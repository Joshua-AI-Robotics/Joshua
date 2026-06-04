import rclpy
from rclpy.node import Node
from std_msgs.msg import Float32

from config.proto import config_pb2
from robot.action.factory import action_factory
from robot.action.proto import action_pb2
from ros2.node_runner import run_node
from ros2.proto import ros2_data_type_pb2
from ros2.utils.packet_parser import (
    PacketParseError,
    action_packet_from_float,
    denormalize_action_packet,
    device_id_from_topic,
)
from ros2.utils.qos_setting import create_qos_setting


class ActuatorEntry:
    def __init__(self, topic, interface, limits, normalized, device_id):
        self.topic = topic
        self.interface = interface
        self.limits = limits
        self.normalized = normalized
        self.device_id = device_id
        self.subscription = None
        self.callback = None


class ActionSubscriber(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)
        self._actuators = []

        for single_action in config.robot.actions.single_actions:
            if (
                single_action.action_type == action_pb2.ActionType.ACTUATOR
                and int(single_action.node.id) != node_id
            ):
                continue

            actuator_proto = single_action.actuator
            qos_setting = single_action.node.qos_setting
            device_id = actuator_proto.actuator_name

            try:
                interface = action_factory.create_action(single_action)
            except Exception as exc:
                self.get_logger().error(
                    f"Failed to create action interface for actuator "
                    f"'{actuator_proto.actuator_name}': {exc}"
                )
                continue

            subscription_count = 0
            for subscription in single_action.node.subscriptions:
                topic = subscription.topic
                data_type = subscription.ros2_data_type

                if data_type != ros2_data_type_pb2.FLOAT32:
                    self.get_logger().error(
                        "Unsupported ros2_data_type %s for actuator '%s'. "
                        "Only FLOAT32 is supported.",
                        str(data_type),
                        topic,
                    )
                    continue

                try:
                    topic_device_id = device_id_from_topic(topic)
                except PacketParseError as exc:
                    self.get_logger().error(
                        f"Invalid actuator Float32 topic '{topic}': {exc}"
                    )
                    continue
                if topic_device_id != device_id:
                    self.get_logger().error(
                        f"Actuator topic '{topic}' device_id '{topic_device_id}' "
                        f"does not match actuator_name '{device_id}'."
                    )
                    continue

                entry = ActuatorEntry(
                    topic=topic,
                    interface=interface,
                    limits=(
                        actuator_proto.operational_lower_limit,
                        actuator_proto.operational_upper_limit,
                    ),
                    normalized=subscription.normalized,
                    device_id=device_id,
                )
                entry.callback = self._make_float32_callback(entry)
                entry.subscription = self.create_subscription(
                    Float32,
                    entry.topic,
                    entry.callback,
                    create_qos_setting(qos_setting),
                )
                self._actuators.append(entry)
                subscription_count += 1

            if subscription_count:
                self.get_logger().info(
                    f"Configured actuator '{actuator_proto.actuator_name}' "
                    f"with {subscription_count} subscription(s)."
                )

        if not self._actuators:
            self.get_logger().error(
                f"No actuators found in configuration for node_id {node_id}!"
            )
            return

        self.get_logger().info(
            f"Actuator subscriber node started with {len(self._actuators)} "
            f"subscriptions for node_id {node_id}!"
        )

    def _make_float32_callback(self, entry: ActuatorEntry):
        """Callback for /<device_id>/<action_type> Float32 actuator topics."""

        def callback(msg: Float32):
            try:
                packet = action_packet_from_float(
                    msg.data,
                    entry.topic,
                    normalized=entry.normalized,
                )
                lower, upper = entry.limits
                packet = denormalize_action_packet(packet, lower, upper)
                entry.interface.set_action(packet)
            except PacketParseError as exc:
                self.get_logger().error(
                    f"Failed to parse Float32 payload for '{entry.topic}': {exc}"
                )
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
