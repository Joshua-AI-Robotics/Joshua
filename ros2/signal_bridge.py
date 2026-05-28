from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Dict

from rclpy.node import Node
from std_msgs.msg import UInt8MultiArray

from config.proto import config_pb2
from robot.action.proto import action_packet_pb2
from robot.perception.proto import perception_packet_pb2
from ros2.node_runner import run_node
from ros2.proto import bridge_pb2, node_pb2, ros2_data_type_pb2
from ros2.utils.qos_setting import create_qos_setting


@dataclass
class MappingRuntime:
    mapping_id: str
    source_topic: str
    target_topic: str
    source_payload_type: int
    mapping_type: int
    action_id: str
    affine: bridge_pb2.AffineTransform
    publisher: object
    subscription: object


class SignalBridge(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)
        self._mappings: Dict[str, MappingRuntime] = {}
        self._publishers: Dict[str, object] = {}

        for single_bridge in config.robot.bridges.single_bridges:
            if (
                int(single_bridge.node.id) != node_id
                or single_bridge.node.node_type != node_pb2.NodeType.SIGNAL_BRIDGE
            ):
                continue

            qos = create_qos_setting(single_bridge.node.qos_setting)
            self._validate_node_topics(single_bridge.node)

            for mapping in single_bridge.bridge.mappings:
                if not mapping.source_topic or not mapping.target_topic:
                    self.get_logger().error(
                        "Mapping '%s' must define source_topic and target_topic.",
                        mapping.mapping_id,
                    )
                    continue

                publisher = self._publishers.get(mapping.target_topic)
                if publisher is None:
                    publisher = self.create_publisher(
                        UInt8MultiArray,
                        mapping.target_topic,
                        qos,
                    )
                    self._publishers[mapping.target_topic] = publisher

                runtime = MappingRuntime(
                    mapping_id=mapping.mapping_id
                    or f"{mapping.source_topic}->{mapping.target_topic}",
                    source_topic=mapping.source_topic,
                    target_topic=mapping.target_topic,
                    source_payload_type=mapping.source_payload_type,
                    mapping_type=mapping.mapping_type,
                    action_id=mapping.action_id,
                    affine=mapping.affine,
                    publisher=publisher,
                    subscription=None,
                )

                runtime.subscription = self.create_subscription(
                    UInt8MultiArray,
                    runtime.source_topic,
                    self._make_callback(runtime),
                    qos,
                )
                self._mappings[runtime.mapping_id] = runtime

        if not self._mappings:
            self.get_logger().error(
                "No signal bridge mappings found for node_id %d!", node_id
            )
            return

        self.get_logger().info(
            "Signal bridge started with %d mapping(s) for node_id %d.",
            len(self._mappings),
            node_id,
        )

    def _validate_node_topics(self, node_cfg: node_pb2.Node) -> None:
        for sub in node_cfg.subscriptions:
            if sub.ros2_data_type != ros2_data_type_pb2.UINT8_MULTI_ARRAY:
                self.get_logger().error(
                    "Unsupported subscription type %s on topic '%s'. "
                    "Signal bridge requires UINT8_MULTI_ARRAY.",
                    str(sub.ros2_data_type),
                    sub.topic,
                )
        for pub in node_cfg.publishers:
            if pub.ros2_data_type != ros2_data_type_pb2.UINT8_MULTI_ARRAY:
                self.get_logger().error(
                    "Unsupported publisher type %s on topic '%s'. "
                    "Signal bridge requires UINT8_MULTI_ARRAY.",
                    str(pub.ros2_data_type),
                    pub.topic,
                )

    def _make_callback(self, runtime: MappingRuntime):
        def callback(msg: UInt8MultiArray):
            action = self._to_action_packet(runtime, msg)
            if action is None:
                return
            out = UInt8MultiArray()
            out.data = action.SerializeToString()
            runtime.publisher.publish(out)

        return callback

    def _to_action_packet(
        self, runtime: MappingRuntime, msg: UInt8MultiArray
    ) -> action_packet_pb2.ActionPacket | None:
        if runtime.source_payload_type == bridge_pb2.PAYLOAD_TYPE_ACTION_PACKET:
            action = action_packet_pb2.ActionPacket()
            try:
                action.ParseFromString(bytes(msg.data))
            except Exception as exc:
                self.get_logger().error(
                    "Failed to parse ActionPacket for mapping '%s': %s",
                    runtime.mapping_id,
                    str(exc),
                )
                return None
            if runtime.mapping_type not in (
                bridge_pb2.MAPPING_TYPE_PASS_THROUGH_ACTION,
                bridge_pb2.MAPPING_TYPE_INVALID,
            ):
                self.get_logger().warning(
                    "Mapping '%s' uses ACTION_PACKET source with mapping_type %s. "
                    "Pass-through is applied.",
                    runtime.mapping_id,
                    str(runtime.mapping_type),
                )
            if runtime.action_id:
                action.action_id = runtime.action_id
            return action

        if runtime.source_payload_type == bridge_pb2.PAYLOAD_TYPE_PERCEPTION_PACKET:
            perception = perception_packet_pb2.PerceptionPacket()
            try:
                perception.ParseFromString(bytes(msg.data))
            except Exception as exc:
                self.get_logger().error(
                    "Failed to parse PerceptionPacket for mapping '%s': %s",
                    runtime.mapping_id,
                    str(exc),
                )
                return None

            if runtime.mapping_type != bridge_pb2.MAPPING_TYPE_POSITION_TO_POSITION:
                self.get_logger().error(
                    "Unsupported mapping_type %s for PERCEPTION_PACKET in mapping '%s'.",
                    str(runtime.mapping_type),
                    runtime.mapping_id,
                )
                return None

            if not perception.HasField("position"):
                self.get_logger().warning(
                    "PerceptionPacket in mapping '%s' has no position field.",
                    runtime.mapping_id,
                )
                return None

            value = float(perception.position.position)
            if runtime.affine.invert:
                value = -value
            # Use identity transform when affine block is omitted in config.
            scale = (
                1.0
                if runtime.affine == bridge_pb2.AffineTransform()
                else runtime.affine.scale
            )
            value = value * scale + runtime.affine.offset
            if runtime.affine.clamp_enabled:
                value = max(
                    runtime.affine.clamp_min, min(runtime.affine.clamp_max, value)
                )

            action = action_packet_pb2.ActionPacket()
            action.position = value
            action.timestamp_ns = (
                perception.timestamp_ns
                if perception.timestamp_ns
                else int(time.time_ns())
            )
            action.action_id = runtime.action_id or runtime.mapping_id
            return action

        self.get_logger().error(
            "Unsupported source_payload_type %s in mapping '%s'.",
            str(runtime.source_payload_type),
            runtime.mapping_id,
        )
        return None


def main() -> int:
    return run_node(SignalBridge, "signal_bridge")


if __name__ == "__main__":
    raise SystemExit(main())
