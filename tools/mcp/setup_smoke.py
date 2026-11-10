"""Bounded SDK stdio smoke for the prepared local inspection MCP server."""

import argparse
import asyncio
import json
import os
import sys
import tempfile
import time
from pathlib import Path

from mcp import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client

EXPECTED_TOOLS = {
    "list_supported_components",
    "validate_config",
    "list_devices",
    "describe_device",
    "get_runtime_status",
    "get_recent_logs",
    "get_feedback",
}


async def smoke(artifacts, config_root, config=None):
    params = StdioServerParameters(
        command=str(artifacts / "tools/mcp/config_validation_server"),
        args=[
            "--validator",
            str(artifacts / "config/joshua_validate_config"),
            "--inspector",
            str(artifacts / "config/joshua_inspect_config"),
            "--config-root",
            str(config_root),
        ],
        env={"LD_LIBRARY_PATH": os.environ.get("LD_LIBRARY_PATH", "")},
    )
    async with stdio_client(params) as (reader, writer):
        async with ClientSession(reader, writer) as client:
            initialized = await client.initialize()
            tools = (await client.list_tools()).tools
            names = {tool.name for tool in tools}
            if names != EXPECTED_TOOLS or not all(
                tool.annotations and tool.annotations.readOnlyHint for tool in tools
            ):
                raise RuntimeError(
                    f"Unexpected read-only tool catalog: {sorted(names)}"
                )
            result = await client.call_tool("list_supported_components", {})
            report = result.structuredContent
            if result.isError or not report or report.get("status_code") != "OK":
                raise RuntimeError(f"Factory discovery failed: {report}")
            if not report.get("data", {}).get("boards"):
                raise RuntimeError("Factory discovery returned no boards")
            output = {
                "ok": True,
                "protocol_version": initialized.protocolVersion,
                "server": initialized.serverInfo.name,
                "tools": sorted(names),
                "list_supported_components": "OK",
            }
            if config is not None:
                result = await client.call_tool(
                    "validate_config", {"config_path": config}
                )
                report = result.structuredContent
                if result.isError or not report or report.get("status_code") != "OK":
                    raise RuntimeError(f"Candidate validation failed: {report}")
                output["validate_config"] = {
                    "config_path": config,
                    "result": report["result"],
                }
            return output


async def bounded_smoke(artifacts, config_root, config):
    return await asyncio.wait_for(smoke(artifacts, config_root, config), timeout=25)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--artifacts", type=Path, default=Path("/opt/joshua-mcp"))
    parser.add_argument("--config-root", type=Path, default=Path("/configs"))
    parser.add_argument("--config", help="Optional relative candidate to validate")
    parser.add_argument("--test", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    started = time.monotonic()
    try:
        if args.test:
            artifacts = Path(os.environ["TEST_SRCDIR"]) / os.environ["TEST_WORKSPACE"]
            with tempfile.TemporaryDirectory() as directory:
                Path(directory, "empty.pbtxt").write_text("")
                report = asyncio.run(
                    bounded_smoke(artifacts, Path(directory), "empty.pbtxt")
                )
                if report["validate_config"]["result"] != "valid":
                    raise RuntimeError("Empty regression candidate was not valid")
        else:
            report = asyncio.run(
                bounded_smoke(args.artifacts, args.config_root, args.config)
            )
        report["duration_seconds"] = round(time.monotonic() - started, 3)
        print(json.dumps(report, sort_keys=True))
        return 0
    except Exception as error:
        print(f"MCP smoke failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
