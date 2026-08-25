import unittest

from robot.action.proto import action_packet_pb2
from tools.pybricks.pybricks_driver import PybricksMotorDriver, SpikeMotorSpec


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

    def run_target(self, hub_id, port, speed, angle):
        self.commands.append((hub_id, port, angle))


class PybricksMotorDriverTest(unittest.TestCase):
    """Unit tests for PybricksMotorDriver using a fake transport (no hardware)."""

    def test_init_set_action_teardown(self):
        transport = FakeTransport()
        spec = SpikeMotorSpec(
            port="A",
            hub_id="hub-1",
            operational_lower_limit=0.0,
            operational_upper_limit=180.0,
        )
        driver = PybricksMotorDriver(spec, transport=transport)

        driver.init()
        self.assertEqual(transport.connected, ["hub-1"])

        packet = action_packet_pb2.ActionPacket()
        packet.position = 42.0
        driver.set_action(packet)
        self.assertEqual(transport.commands, [("hub-1", "A", 42.0)])

        driver.teardown()
        self.assertEqual(transport.disconnected, ["hub-1"])

    def test_hub_id_defaults_to_none(self):
        transport = FakeTransport()
        driver = PybricksMotorDriver(SpikeMotorSpec(port="A"), transport=transport)
        driver.init()
        self.assertEqual(transport.connected, [None])

    def test_requires_port(self):
        with self.assertRaises(ValueError):
            SpikeMotorSpec(port="")


if __name__ == "__main__":
    unittest.main()
