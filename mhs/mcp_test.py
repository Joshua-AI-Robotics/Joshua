"""Run with the mhs venv after building ros_bridge; no hardware is opened."""

import asyncio
import json
import os
import sys
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def main():
    root = Path(__file__).resolve().parent.parent
    params = StdioServerParameters(
        command=sys.executable,
        # The SDK deliberately filters subprocess environment variables; the
        # bridge needs the sourced ROS installation's runtime search paths.
        env={
            key: os.environ[key]
            for key in ("AMENT_PREFIX_PATH", "LD_LIBRARY_PATH", "ROS_DISTRO")
            if key in os.environ
        },
        args=[
            str(root / "mhs/server.py"),
            "--bridge",
            str(root / "bazel-bin/mhs/ros_bridge"),
            "--config",
            str(root / "config/config_preset/example/teensy_hardware_api.pbtxt"),
        ],
    )
    async with stdio_client(params) as (reader, writer):
        async with ClientSession(reader, writer) as session:
            await session.initialize()
            tools = (await session.list_tools()).tools
            assert {tool.name for tool in tools} == {
                "list_devices",
                "describe_device",
                "read_state",
                "write_position",
                "stop_device",
            }
            schema = next(t.inputSchema for t in tools if t.name == "write_position")
            assert schema["properties"]["position_degrees"]["type"] == "number"
            result = await session.call_tool("list_devices", {})
            assert not result.isError
            devices = json.loads(result.content[0].text)["devices"]
            assert len(devices) == 1 and not devices[0]["connected"]
            device_id = devices[0]["device_id"]
            result = await session.call_tool(
                "describe_device", {"device_id": device_id}
            )
            assert not result.isError
            assert json.loads(result.content[0].text)["unit"] == "degrees"
            result = await session.call_tool(
                "write_position", {"device_id": device_id, "position_degrees": 10}
            )
            assert result.isError and "offline" in result.content[0].text
    print(
        "MCP handshake, tool schemas, discovery, description, offline rejection: passed"
    )


if __name__ == "__main__":
    asyncio.run(main())
