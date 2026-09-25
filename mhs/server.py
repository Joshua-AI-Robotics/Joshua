"""Joshua's local stdio MCP adapter, using the official MCP Python SDK."""

import argparse
import signal

from client import RosBridgeClient
from mcp.server.fastmcp import FastMCP
from mcp.types import ToolAnnotations
from session import SessionManager


def create_server(client):
    server = FastMCP(
        "Joshua MHS",
        instructions=(
            "Discover and describe before control. start_session requires explicit "
            "user confirmation: hardware ready, rig clear, reference valid, and "
            "no other process owns the device. Never invent confirmation or "
            "restart/rearm after stop or fault without fresh user confirmation. "
            "Starting does not move. write_position is absolute degrees; poll "
            "completion. End the session when finished. Never automatically retry "
            "uncertain commands."
        ),
    )
    read = ToolAnnotations(readOnlyHint=True, openWorldHint=False)
    write = ToolAnnotations(
        readOnlyHint=False,
        destructiveHint=True,
        idempotentHint=False,
        openWorldHint=False,
    )

    if isinstance(client, SessionManager):

        @server.tool(annotations=write)
        def start_session(hardware_ready: bool, reference_confirmed: bool) -> dict:
            """Start the configured ROS actuator node and connect the bridge.

            Requires explicit user confirmation of the connected, clear rig, no
            competing serial owner, and a valid position reference. Do not infer
            these booleans. Startup disables the driver and reads state; it never
            submits a position target. After stop/fault, end_session and obtain
            fresh user confirmation before starting again.
            """
            return client.start_session(hardware_ready, reference_confirmed)

        @server.tool(annotations=write)
        def end_session() -> dict:
            """Request driver disable and shut down the ROS node owned by this MCP
            session. Inspect disable_acknowledged and errors; process exit cannot
            prove physical stopping. Discovery remains available afterward.
            """
            return client.end_session()

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
        require fresh operator confirmation and a new session.
        """
        return client.request("stop_device", device_id=device_id)

    return server


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--connect-ros", action="store_true")
    parser.add_argument(
        "--actuator-node",
        help="Enable managed MCP sessions with this fixed ROS executable",
    )
    parser.add_argument("--session-lock-dir", default="/tmp/joshua-mhs-locks")
    args = parser.parse_args()
    if args.actuator_node and args.connect_ros:
        parser.error("--actuator-node and --connect-ros are mutually exclusive")
    client = (
        SessionManager(
            args.bridge, args.config, args.actuator_node, args.session_lock_dir
        )
        if args.actuator_node
        else RosBridgeClient(args.bridge, args.config, args.connect_ros)
    )

    def shutdown(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, shutdown)
    try:
        # Validate startup/config before completing the MCP handshake.
        client.request("list_devices")
        create_server(client).run(transport="stdio")
    finally:
        client.close()


if __name__ == "__main__":
    main()
