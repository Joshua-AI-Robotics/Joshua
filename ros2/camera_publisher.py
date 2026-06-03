from __future__ import annotations

from dataclasses import dataclass
from typing import List

from rclpy.node import Node
from sensor_msgs.msg import Image

from config.proto import config_pb2
from robot.perception.factory import perception_factory
from robot.perception.proto import perception_pb2
from ros2.node_runner import run_node
from ros2.proto import ros2_data_type_pb2
from ros2.utils.packet_parser import PacketParseError, require_perception_image
from ros2.utils.qos_setting import create_qos_setting


@dataclass
class CameraEntry:
    topic: str
    interface: object
    publisher: object
    timer: object


class CameraPublisher(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)
        self._cameras: List[CameraEntry] = []

        for single_perception in config.robot.perceptions.single_perceptions:
            if (
                single_perception.perception_type
                != perception_pb2.PerceptionType.CAMERA
                or int(single_perception.node.id) != node_id
            ):
                continue

            camera_proto = single_perception.camera
            qos_setting = single_perception.node.qos_setting

            try:
                interface = perception_factory.create_perception(single_perception)
            except Exception as exc:
                self.get_logger().error(
                    f"Failed to create perception interface for camera "
                    f"'{camera_proto.camera_name}': {exc}"
                )
                continue

            for publisher_cfg in single_perception.node.publishers:
                if publisher_cfg.ros2_data_type != ros2_data_type_pb2.IMAGE:
                    self.get_logger().error(
                        f"Unsupported ros2_data_type {publisher_cfg.ros2_data_type} "
                        f"for camera '{publisher_cfg.topic}'. Only IMAGE is supported."
                    )
                    continue

                self._cameras.append(
                    CameraEntry(
                        topic=publisher_cfg.topic,
                        interface=interface,
                        publisher=self.create_publisher(
                            Image,
                            publisher_cfg.topic,
                            create_qos_setting(qos_setting),
                        ),
                        timer=self.create_timer(
                            1.0 / max(1, publisher_cfg.publish_rate_hz),
                            self._publish_camera_data,
                        ),
                    )
                )

            self.get_logger().info(
                f"Found camera '{camera_proto.camera_name}' in configuration for node_id "
                f"{node_id}. Publishing on {len(single_perception.node.publishers)} topics"
            )

        if not self._cameras:
            self.get_logger().error(
                f"No camera found in configuration for node_id {node_id}!"
            )
            return

        self.get_logger().info(
            f"Camera publisher node started with {len(self._cameras)} cameras for node_id "
            f"{node_id}!"
        )

    def _publish_camera_data(self) -> None:
        if not self._cameras:
            self.get_logger().error("Camera not initialized!")
            return

        for camera in self._cameras:
            try:
                packet = camera.interface.get_data()
            except Exception as exc:
                self.get_logger().warning(
                    f"Failed to get data from camera '{camera.topic}': {exc}"
                )
                continue

            try:
                image_data = require_perception_image(packet)
            except PacketParseError:
                self.get_logger().warning(
                    f"Failed to get image data from camera '{camera.topic}'!"
                )
                continue
            if (
                image_data.width <= 0
                or image_data.height <= 0
                or image_data.channels <= 0
            ):
                self.get_logger().error(
                    f"Invalid image dimensions: {image_data.width}x{image_data.height}, "
                    f"channels={image_data.channels}"
                )
                continue

            image_msg = Image()
            image_msg.header.stamp = self.get_clock().now().to_msg()
            image_msg.header.frame_id = "camera_frame"
            image_msg.height = image_data.height
            image_msg.width = image_data.width
            image_msg.encoding = "rgb8"
            image_msg.is_bigendian = False
            image_msg.step = image_data.width * 3

            data_size = image_data.width * image_data.height * 3
            bgr_bytes = bytes(image_data.data)
            if len(bgr_bytes) < data_size:
                self.get_logger().warning(
                    f"Incomplete image data for camera '{camera.topic}': "
                    f"expected {data_size}, got {len(bgr_bytes)}"
                )
                continue

            rgb = bytearray(data_size)
            for i in range(0, data_size, 3):
                rgb[i] = bgr_bytes[i + 2]
                rgb[i + 1] = bgr_bytes[i + 1]
                rgb[i + 2] = bgr_bytes[i]
            image_msg.data = bytes(rgb)

            camera.publisher.publish(image_msg)


def main() -> int:
    return run_node(CameraPublisher, "camera_publisher")


if __name__ == "__main__":
    raise SystemExit(main())
