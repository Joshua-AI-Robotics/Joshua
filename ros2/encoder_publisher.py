from __future__ import annotations

from dataclasses import dataclass
from typing import List

from rclpy.node import Node
from std_msgs.msg import UInt8MultiArray

from config.proto import config_pb2
from robot.perception.factory import perception_factory
from robot.perception.proto import perception_pb2
from ros2.node_runner import run_node
from ros2.proto import node_pb2, ros2_data_type_pb2
from ros2.utils.packet_parser import (
    PacketParseError,
    require_perception_position,
    serialize_perception_packet,
)
from ros2.utils.qos_setting import create_qos_setting


@dataclass
class EncoderEntry:
    topic: str
    interface: object
    publisher: object
    timer: object


class EncoderPublisher(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)
        self._encoders: List[EncoderEntry] = []

        for single_perception in config.robot.perceptions.single_perceptions:
            if (
                single_perception.perception_type
                != perception_pb2.PerceptionType.ENCODER
                or int(single_perception.node.id) != node_id
            ):
                continue

            encoder_proto = single_perception.encoder
            qos_setting = single_perception.node.qos_setting

            try:
                interface = perception_factory.create_perception(single_perception)
            except Exception as exc:
                self.get_logger().error(
                    f"Failed to create perception interface for encoder "
                    f"'{encoder_proto.encoder_name}': {exc}"
                )
                continue

            for publisher_cfg in single_perception.node.publishers:
                if publisher_cfg.ros2_data_type != ros2_data_type_pb2.UINT8_MULTI_ARRAY:
                    self.get_logger().error(
                        "Unsupported ros2_data_type %s for encoder '%s'. "
                        "Only UINT8_MULTI_ARRAY is supported.",
                        str(publisher_cfg.ros2_data_type),
                        publisher_cfg.topic,
                    )
                    continue

                payload_type = publisher_cfg.payload_type
                if payload_type == node_pb2.PAYLOAD_TYPE_UNSPECIFIED:
                    payload_type = node_pb2.PAYLOAD_TYPE_PERCEPTION_PACKET
                if payload_type == node_pb2.PAYLOAD_TYPE_ACTION_PACKET:
                    self.get_logger().error(
                        f"Encoder publisher '{publisher_cfg.topic}' cannot use "
                        "PAYLOAD_TYPE_ACTION_PACKET."
                    )
                    continue
                if payload_type != node_pb2.PAYLOAD_TYPE_PERCEPTION_PACKET:
                    self.get_logger().error(
                        f"Encoder publisher '{publisher_cfg.topic}' requires "
                        "PAYLOAD_TYPE_PERCEPTION_PACKET."
                    )
                    continue

                self._encoders.append(
                    EncoderEntry(
                        topic=publisher_cfg.topic,
                        interface=interface,
                        publisher=self.create_publisher(
                            UInt8MultiArray,
                            publisher_cfg.topic,
                            create_qos_setting(qos_setting),
                        ),
                        timer=self.create_timer(
                            1.0 / max(1, publisher_cfg.publish_rate_hz),
                            self._publish_encoder_data,
                        ),
                    )
                )

            self.get_logger().info(
                f"Found encoder '{encoder_proto.encoder_name}' in configuration for node_id "
                f"{node_id}. Publishing on {len(single_perception.node.publishers)} topics."
            )

        if not self._encoders:
            self.get_logger().error(
                f"No encoders found in configuration for node_id {node_id}!"
            )
            return

        self.get_logger().info(
            f"Encoder publisher node started with {len(self._encoders)} encoders for node_id "
            f"{node_id}!"
        )

    def _publish_encoder_data(self) -> None:
        if not self._encoders:
            self.get_logger().warning(
                "No encoders initialized, skipping publish cycle."
            )
            return

        try:
            for encoder in self._encoders:
                try:
                    packet = encoder.interface.get_data()
                except Exception as exc:
                    self.get_logger().warning(
                        f"Failed to get data from encoder '{encoder.topic}': {exc}"
                    )
                    continue

                try:
                    require_perception_position(packet)
                except PacketParseError:
                    self.get_logger().warning(
                        f"Failed to get position data from encoder '{encoder.topic}'!"
                    )
                    continue

                msg = UInt8MultiArray()
                msg.data = list(serialize_perception_packet(packet))
                encoder.publisher.publish(msg)

        except Exception as exc:
            self.get_logger().error(f"Error publishing encoder data: {exc}")


def main() -> int:
    return run_node(EncoderPublisher, "encoder_publisher")


if __name__ == "__main__":
    raise SystemExit(main())
