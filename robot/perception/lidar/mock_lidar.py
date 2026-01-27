from __future__ import annotations

import math
import time

from robot.perception.interfaces.lidar_interface import LidarInterface
from robot.perception.proto import perception_packet_pb2, perception_pb2


class MockLidar(LidarInterface):
    """Mock lidar for Python pipeline testing."""

    def __init__(self, lidar_config: perception_pb2.Lidar) -> None:
        self._config = lidar_config
        self._initialized = False

    def init(self) -> None:
        self._initialized = True

    def get_id(self) -> str:
        return f"mock_lidar_{self._config.id}"

    def get_data(self) -> perception_packet_pb2.PerceptionPacket:
        if not self._initialized:
            raise RuntimeError("MockLidar not initialized")

        packet = perception_packet_pb2.PerceptionPacket()
        packet.perception_id = self.get_id()
        packet.timestamp_ns = time.time_ns()

        cloud = packet.point_cloud
        cloud.frame_id = "mock_lidar_frame"
        cloud.is_dense = True

        num_points = 36
        radius = 1.0
        for i in range(num_points):
            angle = (2.0 * math.pi / num_points) * i
            cloud.x.append(radius * math.cos(angle))
            cloud.y.append(radius * math.sin(angle))
            cloud.z.append(0.0)
            cloud.intensity.append(1.0)

        cloud.width = num_points
        cloud.height = 1
        return packet

    def teardown(self) -> None:
        self._initialized = False
