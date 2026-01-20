import math
from dataclasses import dataclass

import rclpy
from rclpy.node import Node
from std_msgs.msg import Float32

from config.proto import config_pb2
from robot.action.factory import action_factory
from robot.action.proto import action_packet_pb2, action_pb2
from robot.perception.proto import perception_pb2
from ros2.node_runner import run_node
from ros2.utils.qos_setting import create_qos_setting


@dataclass
class MappingParams:
    offset: float = 0.0
    multiplier: float = 1.0
    pre_shift: float = 0.0
    mapping_valid: bool = True


class ActuatorEntry:
    def __init__(self, topic, interface, limits, encoder_data_mode):
        self.topic = topic
        self.interface = interface
        self.limits = limits
        self.encoder_data_mode = encoder_data_mode
        self.mapping = self._compute_mapping()
        self.subscription = None

    def _compute_mapping(self) -> MappingParams:
        lower, upper = self.limits
        value_range = upper - lower
        mode = self.encoder_data_mode
        if mode == perception_pb2.ENCODER_DATA_MODE_RAW:
            return MappingParams(
                offset=0.0, pre_shift=0.0, multiplier=1.0, mapping_valid=True
            )
        if mode == perception_pb2.ENCODER_DATA_MODE_NORMALIZED_ZERO_TO_ONE:
            return MappingParams(
                offset=lower, pre_shift=0.0, multiplier=value_range, mapping_valid=True
            )
        if mode == perception_pb2.ENCODER_DATA_MODE_NORMALIZED_MINUS_ONE_TO_ONE:
            return MappingParams(
                offset=lower,
                pre_shift=1.0,
                multiplier=value_range / 2.0,
                mapping_valid=True,
            )
        if mode == perception_pb2.ENCODER_DATA_MODE_NORMALIZED_RADIAN:
            return MappingParams(
                offset=lower,
                pre_shift=math.pi / 2.0,
                multiplier=value_range / math.pi,
                mapping_valid=True,
            )
        return MappingParams(mapping_valid=False)


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
                    entry = ActuatorEntry(
                        topic=subscription.topic,
                        interface=interface,
                        limits=(
                            actuator_proto.operational_lower_limit,
                            actuator_proto.operational_upper_limit,
                        ),
                        encoder_data_mode=actuator_proto.encoder_data_mode,
                    )
                    self._actuators.append(entry)
                    entry.subscription = self.create_subscription(
                        Float32,
                        entry.topic,
                        self._make_callback(entry),
                        create_qos_setting(qos_setting),
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

    def _make_callback(self, entry: ActuatorEntry):
        def callback(msg: Float32):
            if not entry.mapping.mapping_valid:
                self.get_logger().warning(
                    f"Invalid encoder data mode for actuator '{entry.topic}'!"
                )
                return

            mapped_position = (
                entry.mapping.offset
                + (msg.data + entry.mapping.pre_shift) * entry.mapping.multiplier
            )
            packet = action_packet_pb2.ActionPacket()
            packet.position = float(mapped_position)
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
