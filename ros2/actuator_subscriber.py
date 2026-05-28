import rclpy
from rclpy.node import Node
from std_msgs.msg import UInt8MultiArray

from config.proto import config_pb2
from robot.action.factory import action_factory
from robot.action.proto import action_packet_pb2, action_pb2
from ros2.node_runner import run_node
from ros2.proto import ros2_data_type_pb2
from ros2.utils.qos_setting import create_qos_setting


class ActuatorEntry:
    def __init__(self, topic, interface, data_type):
        self.topic = topic
        self.interface = interface
        self.data_type = data_type
        self.subscription = None
        self.callback = None


class ActionSubscriber(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)
        self._actuators = []

        for single_action in config.robot.actions.single_actions:
            if (
                single_action.action_type == action_pb2.ActionType.ACTUATOR
                and int(single_action.node.id) == node_id
            ):
                actuator_proto = single_action.actuator
                qos_setting = single_action.node.qos_setting

                try:
                    interface = action_factory.create_action(single_action)
                except Exception as exc:
                    self.get_logger().error(
                        f"Failed to create action interface for actuator "
                        f"'{actuator_proto.actuator_name}': {exc}"
                    )
                    continue

                for subscription in single_action.node.subscriptions:
                    data_type = subscription.ros2_data_type
                    entry = ActuatorEntry(
                        topic=subscription.topic,
                        interface=interface,
                        data_type=data_type,
                    )
                    self._actuators.append(entry)

                    if data_type == ros2_data_type_pb2.UINT8_MULTI_ARRAY:
                        entry.callback = self._make_uint8_multi_array_callback(entry)
                        entry.subscription = self.create_subscription(
                            UInt8MultiArray,
                            entry.topic,
                            entry.callback,
                            create_qos_setting(qos_setting),
                        )
                    else:
                        self.get_logger().error(
                            "Unsupported ros2_data_type %s for actuator '%s'. "
                            "Only UINT8_MULTI_ARRAY is supported.",
                            str(data_type),
                            entry.topic,
                        )

        if not self._actuators:
            self.get_logger().error(
                f"No actuators found in configuration for node_id {node_id}!"
            )
            return

        self.get_logger().info(
            f"Actuator subscriber node started with {len(self._actuators)} actuators for "
            f"node_id {node_id}!"
        )

    def _make_uint8_multi_array_callback(self, entry: ActuatorEntry):
        """Callback for UINT8_MULTI_ARRAY topics carrying binary ActionPacket."""

        def callback(msg: UInt8MultiArray):
            packet = action_packet_pb2.ActionPacket()
            try:
                packet.ParseFromString(bytes(msg.data))
            except Exception as exc:
                self.get_logger().error(
                    f"Failed to parse binary ActionPacket for '{entry.topic}': {exc}"
                )
                return
            try:
                entry.interface.set_action(packet)
            except Exception as exc:
                self.get_logger().error(
                    f"Failed to set action for actuator '{entry.topic}': {exc}"
                )

        return callback

    def shutdown(self):
        for actuator in self._actuators:
            try:
                actuator.interface.teardown()
            except Exception as exc:
                self.get_logger().error(
                    f"Failed to teardown actuator '{actuator.topic}': {exc}"
                )


def main() -> int:
    return run_node(ActionSubscriber, "actuator_subscriber")


if __name__ == "__main__":
    raise SystemExit(main())
