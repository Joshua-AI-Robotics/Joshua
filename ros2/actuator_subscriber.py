import rclpy
from rclpy.node import Node
from std_msgs.msg import UInt8MultiArray

from config.proto import config_pb2
from robot.action.factory import action_factory
from robot.action.proto import action_packet_pb2, action_pb2
from robot.perception.proto import perception_packet_pb2
from ros2.node_runner import run_node
from ros2.proto import node_pb2, ros2_data_type_pb2
from ros2.utils.qos_setting import create_qos_setting


def _map_normalized_position(value: float, lower: float, upper: float) -> float:
    """Map normalized [-1, 1] to raw ticks in [lower, upper]."""
    normalized = max(-1.0, min(1.0, float(value)))
    return lower + (normalized + 1.0) * (upper - lower) / 2.0


def _denormalize_position_value(value: float, lower: float, upper: float) -> float:
    """Map normalized position to raw ticks and clamp to operational limits."""
    position = _map_normalized_position(value, lower, upper)
    return max(lower, min(upper, position))


class ActuatorEntry:
    def __init__(self, topic, interface, data_type, payload_type, limits):
        self.topic = topic
        self.interface = interface
        self.data_type = data_type
        self.payload_type = payload_type
        self.limits = limits
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
                        payload_type=subscription.payload_type,
                        limits=(
                            actuator_proto.operational_lower_limit,
                            actuator_proto.operational_upper_limit,
                        ),
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

    def _parse_action_packet(self, entry: ActuatorEntry, data: bytes):
        if entry.payload_type in (
            node_pb2.PAYLOAD_TYPE_UNSPECIFIED,
            node_pb2.PAYLOAD_TYPE_ACTION_PACKET,
        ):
            packet = action_packet_pb2.ActionPacket()
            # TODO: check ParseFromString return value; reject corrupt payloads.
            packet.ParseFromString(data)
            return packet

        if entry.payload_type == node_pb2.PAYLOAD_TYPE_PERCEPTION_PACKET:
            perception = perception_packet_pb2.PerceptionPacket()
            # TODO: check ParseFromString return value; reject corrupt payloads.
            perception.ParseFromString(data)
            if not perception.HasField("position"):
                raise ValueError("PerceptionPacket has no position field")
            packet = action_packet_pb2.ActionPacket()
            packet.position = float(perception.position.position)
            if perception.HasField("timestamp_ns"):
                packet.timestamp_ns = perception.timestamp_ns
            return packet

        raise ValueError(f"Unsupported payload_type {entry.payload_type}")

    def _prepare_action_packet(self, entry: ActuatorEntry, packet):
        if not packet.normalized:
            return packet
        lower, upper = entry.limits
        if packet.HasField("position"):
            packet.position = _denormalize_position_value(packet.position, lower, upper)
        if packet.HasField("complex") and packet.complex.HasField("position"):
            packet.complex.position = _denormalize_position_value(
                packet.complex.position, lower, upper
            )
        return packet

    def _make_uint8_multi_array_callback(self, entry: ActuatorEntry):
        """Callback for UINT8_MULTI_ARRAY topics carrying ActionPacket or PerceptionPacket."""

        def callback(msg: UInt8MultiArray):
            try:
                packet = self._parse_action_packet(entry, bytes(msg.data))
            except Exception as exc:
                self.get_logger().error(
                    f"Failed to parse subscription payload for '{entry.topic}': {exc}"
                )
                return

            try:
                packet = self._prepare_action_packet(entry, packet)
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
