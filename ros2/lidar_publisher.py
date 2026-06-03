from __future__ import annotations

from dataclasses import dataclass
from typing import List

from rclpy.node import Node
from sensor_msgs.msg import PointCloud2, PointField
from sensor_msgs_py import point_cloud2
from std_msgs.msg import Header, UInt8MultiArray

from config.proto import config_pb2
from robot.perception.factory import perception_factory
from robot.perception.proto import perception_pb2
from ros2.node_runner import run_node
from ros2.proto import node_pb2, ros2_data_type_pb2
from ros2.utils.packet_parser import (
    PacketParseError,
    require_perception_point_cloud,
    serialize_perception_packet,
)
from ros2.utils.qos_setting import create_qos_setting


@dataclass
class LidarEntry:
    topic: str
    interface: object
    publisher: object
    timer: object
    frame_id: str
    ros2_data_type: int
    payload_type: int


class LidarPublisher(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)
        self._lidars: List[LidarEntry] = []

        for single_perception in config.robot.perceptions.single_perceptions:
            if (
                single_perception.perception_type != perception_pb2.PerceptionType.LIDAR
                or int(single_perception.node.id) != node_id
            ):
                continue

            lidar_proto = single_perception.lidar
            qos_setting = single_perception.node.qos_setting

            try:
                interface = perception_factory.create_perception(single_perception)
            except Exception as exc:
                self.get_logger().error(
                    f"Failed to create perception interface for lidar "
                    f"'{lidar_proto.lidar_name}': {exc}"
                )
                continue

            for publisher_cfg in single_perception.node.publishers:
                payload_type = publisher_cfg.payload_type
                if payload_type == node_pb2.PAYLOAD_TYPE_UNSPECIFIED:
                    payload_type = node_pb2.PAYLOAD_TYPE_PERCEPTION_PACKET
                if payload_type == node_pb2.PAYLOAD_TYPE_ACTION_PACKET:
                    self.get_logger().error(
                        f"Lidar publisher '{publisher_cfg.topic}' cannot use "
                        "PAYLOAD_TYPE_ACTION_PACKET."
                    )
                    continue
                if publisher_cfg.ros2_data_type == ros2_data_type_pb2.UINT8_MULTI_ARRAY:
                    if payload_type != node_pb2.PAYLOAD_TYPE_PERCEPTION_PACKET:
                        self.get_logger().error(
                            f"Lidar publisher '{publisher_cfg.topic}' requires "
                            "PAYLOAD_TYPE_PERCEPTION_PACKET for UINT8_MULTI_ARRAY."
                        )
                        continue
                elif publisher_cfg.ros2_data_type != ros2_data_type_pb2.POINTCLOUD2:
                    self.get_logger().error(
                        f"Unsupported ros2_data_type {publisher_cfg.ros2_data_type} "
                        f"for lidar '{publisher_cfg.topic}'."
                    )
                    continue

                if publisher_cfg.ros2_data_type == ros2_data_type_pb2.UINT8_MULTI_ARRAY:
                    msg_type = UInt8MultiArray
                else:
                    msg_type = PointCloud2

                self._lidars.append(
                    LidarEntry(
                        topic=publisher_cfg.topic,
                        interface=interface,
                        publisher=self.create_publisher(
                            msg_type,
                            publisher_cfg.topic,
                            create_qos_setting(qos_setting),
                        ),
                        timer=self.create_timer(
                            1.0 / max(1, publisher_cfg.publish_rate_hz),
                            self._publish_lidar_data,
                        ),
                        frame_id=lidar_proto.lidar_name or "lidar_frame",
                        ros2_data_type=publisher_cfg.ros2_data_type,
                        payload_type=payload_type,
                    )
                )

            self.get_logger().info(
                f"Found lidar '{lidar_proto.lidar_name}' in configuration for node_id "
                f"{node_id}. Publishing on {len(single_perception.node.publishers)} topics"
            )

        if not self._lidars:
            self.get_logger().error(
                f"No lidars found in configuration for node_id {node_id}!"
            )
            return

        self.get_logger().info(
            f"Lidar publisher node started with {len(self._lidars)} lidars for node_id {node_id}!"
        )

    def _publish_lidar_data(self) -> None:
        if not self._lidars:
            self.get_logger().warning("No lidars initialized, skipping publish cycle.")
            return

        try:
            for lidar in self._lidars:
                try:
                    packet = lidar.interface.get_data()
                except Exception as exc:
                    self.get_logger().warning(
                        f"Failed to get data from lidar '{lidar.topic}': {exc}"
                    )
                    continue

                try:
                    cloud = require_perception_point_cloud(packet)
                except PacketParseError:
                    self.get_logger().warning(
                        f"LiDAR '{lidar.topic}' packet has no point cloud!"
                    )
                    continue

                if lidar.ros2_data_type == ros2_data_type_pb2.UINT8_MULTI_ARRAY:
                    msg = UInt8MultiArray()
                    msg.data = list(serialize_perception_packet(packet))
                    lidar.publisher.publish(msg)
                    continue

                num_points = len(cloud.x)
                if num_points == 0:
                    self.get_logger().warning(
                        f"Empty point cloud from '{lidar.topic}'!"
                    )
                    continue

                intensity = list(cloud.intensity)
                if len(intensity) < num_points:
                    intensity.extend([0.0] * (num_points - len(intensity)))

                points = list(zip(cloud.x, cloud.y, cloud.z, intensity))
                header = Header()
                header.stamp = self.get_clock().now().to_msg()
                header.frame_id = cloud.frame_id or lidar.frame_id

                fields = [
                    PointField(
                        name="x", offset=0, datatype=PointField.FLOAT32, count=1
                    ),
                    PointField(
                        name="y", offset=4, datatype=PointField.FLOAT32, count=1
                    ),
                    PointField(
                        name="z", offset=8, datatype=PointField.FLOAT32, count=1
                    ),
                    PointField(
                        name="intensity",
                        offset=12,
                        datatype=PointField.FLOAT32,
                        count=1,
                    ),
                ]

                cloud_msg = point_cloud2.create_cloud(header, fields, points)
                cloud_msg.height = cloud.height if cloud.height > 0 else 1
                cloud_msg.width = cloud.width if cloud.width > 0 else num_points
                cloud_msg.is_dense = cloud.is_dense

                lidar.publisher.publish(cloud_msg)
        except Exception as exc:
            self.get_logger().error(f"Error publishing lidar data: {exc}")


def main() -> int:
    return run_node(LidarPublisher, "lidar_publisher")


if __name__ == "__main__":
    raise SystemExit(main())
