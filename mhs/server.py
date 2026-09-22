"""Joshua's local stdio MCP adapter, using the official MCP Python SDK."""

import argparse

from client import RosBridgeClient
from mcp.server.fastmcp import FastMCP
from mcp.types import ToolAnnotations


def create_server(client):
    server = FastMCP("Joshua MHS")
    read = ToolAnnotations(readOnlyHint=True, openWorldHint=False)
    write = ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=True,
        idempotentHint=False,
        openWorldHint=False,
    )

    @server.tool(annotations=read)
    def list_devices() -> dict:
        """Discover configured devices and their capabilities without hardware I/O."""
        return client.request("list_devices")

    @server.tool(annotations=read)
    def describe_device(device_id: str) -> dict:
        """Read a device's description, units, limits and feedback limitations."""
        return client.request("describe_device", device_id=device_id)

    @server.tool(annotations=ToolAnnotations(readOnlyHint=False, openWorldHint=False))
    def read_state(device_id: str) -> dict:
        """Read fresh controller feedback and command status. Emitted steps do not
        verify physical position. Faults cause the runtime to request disable.
        """
        return client.request("read_state", device_id=device_id)

    @server.tool(annotations=write)
    def write_position(device_id: str, position_degrees: float) -> dict:
        """Submit a bounded absolute move in degrees. Read describe_device first.
        Acceptance is not completion: poll read_state until status changes from
        moving. Never automatically retry an ambiguous/failed write. The runtime
        rejects overlapping moves and enforces the configured duration limit.
        """
        return client.request(
            "write_position", device_id=device_id, position_degrees=position_degrees
        )

    @server.tool(annotations=write)
    def stop_device(device_id: str) -> dict:
        """Request driver disable and invalidate the coordinate reference. Check
        disable_acknowledged; this cannot verify physical stopping. Further moves
        require an operator restart and reference confirmation, unavailable to LLMs.
        """
        return client.request("stop_device", device_id=device_id)

    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--connect-ros", action="store_true")
    args = parser.parse_args()
    client = RosBridgeClient(args.bridge, args.config, args.connect_ros)
    try:
        # Validate startup/config before completing the MCP handshake.
        client.request("list_devices")
        create_server(client).run(transport="stdio")
    finally:
        client.close()


if __name__ == "__main__":
    main()
