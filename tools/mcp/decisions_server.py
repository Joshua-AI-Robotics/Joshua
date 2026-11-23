import argparse
import asyncio
import os

from mcp import types
from mcp.server.lowlevel import Server
from mcp.server.stdio import stdio_server
from openai import AsyncOpenAI

from tools.mcp.decisions_request import DecisionsRequests
from tools.mcp.snapshot_observer import SnapshotObserver


def make_server(requests):
    app = Server("joshua-decisions")
    tool = types.Tool(
        name="request_decision",
        description=(
            "Send the operator's whole Joshua request verbatim. Decisions chooses the "
            "route. Display the reply. Start sequence at 1 per connection, increment "
            "for each new turn, and reuse the sequence and text to retry."
        ),
        inputSchema={
            "type": "object",
            "properties": {
                "text": {"type": "string", "minLength": 1, "maxLength": 2000},
                "sequence": {"type": "integer", "minimum": 1},
            },
            "required": ["text", "sequence"],
            "additionalProperties": False,
        },
        annotations=types.ToolAnnotations(
            readOnlyHint=requests.replay is None,
            destructiveHint=False,
            openWorldHint=True,
            idempotentHint=True,
        ),
    )

    stop = types.Tool(
        name="stop_decision",
        description="Cancel a pending replay turn locally using its sequence.",
        inputSchema={
            "type": "object",
            "properties": {"sequence": {"type": "integer", "minimum": 1}},
            "required": ["sequence"],
            "additionalProperties": False,
        },
        annotations=types.ToolAnnotations(
            readOnlyHint=False,
            destructiveHint=False,
            openWorldHint=False,
            idempotentHint=True,
        ),
    )

    @app.list_tools()
    async def list_tools():
        return [tool, stop] if requests.replay is not None else [tool]

    @app.call_tool()
    async def call_tool(name, arguments):
        if name == stop.name and requests.replay is not None:
            status = requests.stop(arguments["sequence"])
            return types.CallToolResult(
                content=[types.TextContent(type="text", text=status)],
                structuredContent={"sequence": arguments["sequence"], "status": status},
            )
        if name != tool.name:
            raise ValueError("Unknown tool.")
        report = await requests.request(**arguments)
        return types.CallToolResult(
            content=[types.TextContent(type="text", text=report["reply"])],
            structuredContent=report,
            isError=report["outcome"] in {"failed", "timeout"},
        )

    return app


async def serve(args):
    observer = SnapshotObserver(args.observation_dir)
    try:
        async with AsyncOpenAI(
            api_key=os.environ["OPENAI_API_KEY"],
            base_url="https://api.openai.com/v1",
            timeout=18,
            max_retries=0,
        ) as api:
            replay = None
            if args.head_replay:
                from tools.mcp.head_replay import HeadReplay

                replay = HeadReplay.load()
            requests = DecisionsRequests(api, observer, replay=replay)
            app = make_server(requests)
            async with stdio_server() as (reader, writer):
                try:
                    await app.run(reader, writer, app.create_initialization_options())
                finally:
                    requests.bind_replay(None)
    finally:
        await observer.aclose()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--observation-dir")
    parser.add_argument("--head-replay", action="store_true")
    asyncio.run(serve(parser.parse_args()))
