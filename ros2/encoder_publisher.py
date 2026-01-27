from __future__ import annotations

import math
from dataclasses import dataclass
from typing import List

from rclpy.node import Node
from std_msgs.msg import Float32

from config.proto import config_pb2
from robot.perception.factory import perception_factory
from robot.perception.proto import perception_pb2
from ros2.node_runner import run_node
from ros2.utils.qos_setting import create_qos_setting


@dataclass
class EncoderEntry:
    topic: str
    interface: object
    limits: tuple
    encoder_data_mode: int
    publisher: object
    timer: object


class EncoderPublisher(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)
        self._encoders: List[EncoderEntry] = []

        for single_perception in config.robot.perceptions.single_perceptions:
            if (
                single_perception.perception_type
                == perception_pb2.PerceptionType.ENCODER
                and int(single_perception.node.id) == node_id
            ):
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

                for publisher in single_perception.node.publishers:
                    self._encoders.append(
                        EncoderEntry(
                            topic=publisher.topic,
                            interface=interface,
                            limits=(
                                encoder_proto.operational_lower_limit,
                                encoder_proto.operational_upper_limit,
                            ),
                            encoder_data_mode=encoder_proto.encoder_data_mode,
                            publisher=self.create_publisher(
                                Float32,
                                publisher.topic,
                                create_qos_setting(qos_setting),
                            ),
                            timer=self.create_timer(
                                1.0 / max(1, publisher.publish_rate_hz),
                                self._publish_encoder_data,
                            ),
                        )
                    )

                self.get_logger().info(
                    f"Found encoder '{encoder_proto.encoder_name}' in configuration for node_id "
                    f"{node_id}. Publishing on {len(single_perception.node.publishers)} topics "
                    f"with data mode: {encoder_proto.encoder_data_mode}"
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

                if not packet.HasField("position"):
                    self.get_logger().warning(
                        f"Failed to get position data from encoder '{encoder.topic}'!"
                    )
                    continue

                position_data = self._normalize_position(
                    packet.position.position, encoder
                )
                if position_data is None:
                    continue

                message = Float32()
                message.data = float(position_data)
                encoder.publisher.publish(message)
                self.get_logger().info(
                    f"Published encoder data to topic '{encoder.topic}'"
                )
        except Exception as exc:
            self.get_logger().error(f"Error publishing encoder data: {exc}")

    def _normalize_position(self, position, encoder: EncoderEntry) -> float | None:
        position_data = float(position)
        lower, upper = encoder.limits

        if encoder.encoder_data_mode == perception_pb2.ENCODER_DATA_MODE_RAW:
            return position_data
        if (
            encoder.encoder_data_mode
            == perception_pb2.ENCODER_DATA_MODE_NORMALIZED_ZERO_TO_ONE
        ):
            position_data = (position_data - lower) / (upper - lower)
            return max(0.0, min(1.0, position_data))
        if (
            encoder.encoder_data_mode
            == perception_pb2.ENCODER_DATA_MODE_NORMALIZED_MINUS_ONE_TO_ONE
        ):
            position_data = 2.0 * (position_data - lower) / (upper - lower) - 1.0
            return max(-1.0, min(1.0, position_data))
        if (
            encoder.encoder_data_mode
            == perception_pb2.ENCODER_DATA_MODE_NORMALIZED_RADIAN
        ):
            position_data = (math.pi * (position_data - lower) / (upper - lower)) - (
                math.pi / 2.0
            )
            return max(-math.pi / 2.0, min(math.pi / 2.0, position_data))

        self.get_logger().warning(
            f"Invalid publish data mode for encoder '{encoder.topic}'!"
        )
        return None


def main() -> int:
    return run_node(EncoderPublisher, "encoder_publisher")


if __name__ == "__main__":
    raise SystemExit(main())
