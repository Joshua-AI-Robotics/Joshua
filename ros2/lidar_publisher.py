from __future__ import annotations

from dataclasses import dataclass
from typing import List

from rclpy.node import Node
from sensor_msgs.msg import PointCloud2, PointField
from sensor_msgs_py import point_cloud2
from std_msgs.msg import Header

from config.proto import config_pb2
from robot.perception.factory import perception_factory
from robot.perception.proto import perception_pb2
from ros2.node_runner import run_node
from ros2.utils.qos_setting import create_qos_setting


@dataclass
class LidarEntry:
    topic: str
    interface: object
    publisher: object
    timer: object
    frame_id: str


class LidarPublisher(Node):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)
        self._lidars: List[LidarEntry] = []

        for single_perception in config.robot.perceptions.single_perceptions:
            if (
                single_perception.perception_type == perception_pb2.PerceptionType.LIDAR
                and int(single_perception.node.id) == node_id
            ):
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

                for publisher in single_perception.node.publishers:
                    self._lidars.append(
                        LidarEntry(
                            topic=publisher.topic,
                            interface=interface,
                            publisher=self.create_publisher(
                                PointCloud2,
                                publisher.topic,
                                create_qos_setting(qos_setting),
                            ),
                            timer=self.create_timer(
                                1.0 / max(1, publisher.publish_rate_hz),
                                self._publish_lidar_data,
                            ),
                            frame_id=lidar_proto.lidar_name or "lidar_frame",
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

                if not packet.HasField("point_cloud"):
                    self.get_logger().warning(
                        f"LiDAR '{lidar.topic}' packet has no point cloud!"
                    )
                    continue

                cloud = packet.point_cloud
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
