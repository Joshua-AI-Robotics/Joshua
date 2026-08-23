import sys
import time

from robot.action.proto import action_packet_pb2, action_pb2
from robot.comm.proto import comm_pb2
from tools.pybricks.pybricks_driver import PybricksMotorDriver


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

    actuator = action_pb2.Actuator()
    actuator.actuator_name = "spike_motor"
    actuator.id = 1
    actuator.actuator_type = action_pb2.ActuatorType.SPIKE_MOTOR
    actuator.comm.comm_type = comm_pb2.BLE
    actuator.spike_motor_config.hub_id = hub_id if hub_id != "''" else ""
    actuator.spike_motor_config.port = port

    driver = PybricksMotorDriver(actuator)
    driver.init()

    packet = action_packet_pb2.ActionPacket()
    packet.position = angle
    driver.set_action(packet)
    time.sleep(1.0)

    driver.teardown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
