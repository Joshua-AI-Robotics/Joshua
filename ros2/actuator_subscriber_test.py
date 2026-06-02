import unittest
from unittest import mock


class ActuatorSubscriberTest(unittest.TestCase):
    """Regression test for actuator subscriber callback wiring without hardware."""

    def test_callback_mapping_raw(self):
        try:
            import rclpy
        except Exception as exc:
            self.skipTest(f"rclpy not available: {exc}")

        from actuator_subscriber import ActionSubscriber
        from std_msgs.msg import Float32

        from config.proto import config_pb2
        from robot.action.proto import action_pb2

        class FakeDriver:
            def __init__(self):
                self.last_packet = None

            def init(self):
                pass

            def set_action(self, packet):
                self.last_packet = packet

            def teardown(self):
                pass

        rclpy.init(args=[])
        try:
            fake_driver = FakeDriver()

            def fake_create_action(_):
                return fake_driver

            cfg = config_pb2.Config()
            cfg.general.name = "test"
            cfg.general.id = 1
            action = cfg.robot.actions.single_actions.add()
            action.action_type = action_pb2.ActionType.ACTUATOR
            action.node.id = 123
            action.node.node_type = action.node.ACTUATOR_SUBSCRIBER
            action.node.qos_setting.depth = 1
            sub = action.node.subscriptions.add()
            sub.ros2_data_type = sub.ROS2_DATA_TYPE_FLOAT32
            sub.topic = "spike/motor_A/command"
            actuator = action.actuator
            actuator.actuator_name = "spike_motor_A"
            actuator.id = 1
            actuator.actuator_type = action_pb2.ActuatorType.MOCK_MOTOR
            actuator.mock_motor_config.motor_id = 1
            actuator.operational_lower_limit = 0
            actuator.operational_upper_limit = 1000

            with mock.patch(
                "ros2.actuator_subscriber.action_factory.create_action",
                side_effect=fake_create_action,
            ):
                node = ActionSubscriber("actuator_subscriber", 123, cfg)
                try:
                    self.assertEqual(len(node._actuators), 1)
                    entry = node._actuators[0]
                    msg = Float32()
                    msg.data = 42.0
                    entry.callback(msg)
                    self.assertIsNotNone(fake_driver.last_packet)
                    self.assertEqual(fake_driver.last_packet.position, 42.0)
                finally:
                    node.shutdown()
                    node.destroy_node()
        finally:
            if rclpy.ok():
                rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
