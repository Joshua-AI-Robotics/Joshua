import unittest

from robot.action.motors.drivers.pybricks_driver import PybricksMotorDriver
from robot.action.proto import action_packet_pb2, action_pb2
from robot.comm.proto import comm_pb2


class FakeTransport:
    def __init__(self) -> None:
        self.connected = []
        self.disconnected = []
        self.commands = []

    def connect(self, hub_id):
        self.connected.append(hub_id)

    def disconnect(self, hub_id):
        self.disconnected.append(hub_id)

    def set_motor_angle(self, hub_id, port, angle):
        self.commands.append((hub_id, port, angle))


class PybricksMotorDriverTest(unittest.TestCase):
    """Unit tests for PybricksMotorDriver using a fake transport (no hardware)."""
    def _make_actuator(self, hub_id: str = "", port: str = "A"):
        actuator = action_pb2.Actuator()
        actuator.actuator_name = "spike_motor_A"
        actuator.id = 1
        actuator.actuator_type = action_pb2.ActuatorType.SPIKE_MOTOR
        actuator.comm.comm_type = comm_pb2.BLE
        actuator.spike_motor_config.hub_id = hub_id
        actuator.spike_motor_config.port = port
        return actuator

    def test_init_set_action_teardown(self):
        transport = FakeTransport()
        actuator = self._make_actuator(hub_id="hub-1", port="A")
        driver = PybricksMotorDriver(actuator, transport=transport)

        driver.init()
        self.assertEqual(transport.connected, ["hub-1"])

        packet = action_packet_pb2.ActionPacket()
        packet.position = 42.0
        driver.set_action(packet)
        self.assertEqual(transport.commands, [("hub-1", "A", 42.0)])

        driver.teardown()
        self.assertEqual(transport.disconnected, ["hub-1"])

    def test_requires_ble_comm(self):
        transport = FakeTransport()
        actuator = self._make_actuator()
        actuator.comm.comm_type = comm_pb2.SERIAL
        with self.assertRaises(ValueError):
            PybricksMotorDriver(actuator, transport=transport)

    def test_requires_port(self):
        transport = FakeTransport()
        actuator = self._make_actuator(port="")
        with self.assertRaises(ValueError):
            PybricksMotorDriver(actuator, transport=transport)


if __name__ == "__main__":
    unittest.main()
