import sys
import time

from robot.action.proto import action_packet_pb2
from tools.pybricks.pybricks_driver import PybricksMotorDriver, SpikeMotorSpec


def _usage() -> None:
    print("Usage: pybricks_ble_smoke <hub_id_or_empty> <port> <angle>")
    print("Example: pybricks_ble_smoke SPIKE  A  90")


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        _usage()
        return 2

    hub_id = argv[1]
    port = argv[2]
    try:
        angle = float(argv[3])
    except ValueError:
        _usage()
        return 2

    spec = SpikeMotorSpec(
        port=port,
        hub_id=(hub_id if hub_id and hub_id != "''" else None),
    )

    driver = PybricksMotorDriver(spec)
    driver.init()

    packet = action_packet_pb2.ActionPacket()
    packet.position = angle
    driver.set_action(packet)
    time.sleep(1.0)

    driver.teardown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
