import asyncio
import json
import os
import sys
from datetime import timedelta

from mcp import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client

CASES = (
    ("Look at the red mug.", "head", ("completed",), {"joint": "pan", "radians": 0.1}),
    ("Tell me where the red mug is, don't move.", "scene", ("completed",), None),
    ("Look at the green bottle.", "head", ("no_command", "low_confidence"), None),
)


def record(event, **fields):
    print(json.dumps({"event": event, **fields}), flush=True)


async def run_turns(client):
    passed = 0
    for sequence, (text, route, outcomes, command) in enumerate(CASES, 1):
        expected = {"route": route, "outcomes": outcomes, "command": command}
        record("request", sequence=sequence, text=text, expected=expected)
        print(f"\n{sequence}. {text}", file=sys.stderr, flush=True)
        result = await client.call_tool(
            "request_decision", {"sequence": sequence, "text": text}
        )
        report = result.structuredContent or {}
        actual_command = report.get("replay_command")
        reported_reply = report.get("reply")
        matched = {
            "sequence": type(report.get("sequence")) is int
            and report["sequence"] == sequence,
            "reply": isinstance(reported_reply, str) and bool(reported_reply.strip()),
            "route": report.get("route") == route,
            "outcome": report.get("outcome") in outcomes,
            "command": (
                actual_command == command
                if command is not None
                else bool(report) and "replay_command" not in report
            ),
        }
        if report.get("outcome") == "no_command" and route == "head":
            motor = report.get("motor")
            matched["missing_target"] = isinstance(motor, dict) and (
                motor.get("choice") == "missing"
            )
        reply = (
            reported_reply
            if matched["reply"]
            else "\n".join(item.text for item in result.content if item.type == "text")
        )
        ok = not result.isError and all(matched.values())
        record(
            "result",
            sequence=sequence,
            matched=matched,
            ok=ok,
            tool_error=result.isError,
            reply=reply,
            report=report,
        )
        if not report:
            target = "unknown"
        elif actual_command is None:
            target = "none"
        elif (
            isinstance(actual_command, dict)
            and actual_command.get("joint") == "pan"
            and type(actual_command.get("radians")) is float
        ):
            target = f"pan {actual_command['radians']:+.3f} rad"
        else:
            target = json.dumps(actual_command)
        status = "PASS" if ok else "MISMATCH"
        print(
            f"   {status} | route={report.get('route')} | "
            f"outcome={report.get('outcome')} | target={target}\n"
            f"   {reply}",
            file=sys.stderr,
        )
        if not ok:
            print(f"   Expected {json.dumps(expected)}", file=sys.stderr)
        passed += int(ok)
    record("summary", passed=passed, total=len(CASES))
    print(f"\n{passed}/{len(CASES)} turns matched.", file=sys.stderr)
    return passed == len(CASES)


async def demo():
    params = StdioServerParameters(
        command="/opt/joshua-mcp/tools/mcp/decisions_server",
        args=["--head-replay"],
        env={
            "OPENAI_API_KEY": os.environ["OPENAI_API_KEY"],
            "LD_LIBRARY_PATH": os.environ.get("LD_LIBRARY_PATH", ""),
        },
    )
    async with stdio_client(params) as (reader, writer):
        async with ClientSession(
            reader, writer, read_timeout_seconds=timedelta(seconds=30)
        ) as client:
            initialized = await client.initialize()
            names = {tool.name for tool in (await client.list_tools()).tools}
            if (
                initialized.serverInfo.name != "joshua-decisions"
                or not {"request_decision", "stop_decision"} <= names
            ):
                raise RuntimeError("Expected the Joshua Decisions head replay.")
            return await run_turns(client)


if __name__ == "__main__":
    try:
        raise SystemExit(0 if asyncio.run(demo()) else 1)
    except Exception as exc:
        record("error", error=type(exc).__name__)
        print(
            "Replay session failed or disconnected. See the last recorded turn.",
            file=sys.stderr,
        )
        raise SystemExit(1)
