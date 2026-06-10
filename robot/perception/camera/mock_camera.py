from __future__ import annotations

import time

from robot.perception.interfaces.camera_interface import CameraInterface
from robot.perception.proto import perception_packet_pb2, perception_pb2


class MockCamera(CameraInterface):
    """Mock camera for Python pipeline testing."""

    def __init__(self, camera_config: perception_pb2.Camera) -> None:
        self._config = camera_config
        self._initialized = False

    def init(self) -> None:
        self._initialized = True

    def get_id(self) -> str:
        return f"mock_camera_{self._config.id}"

    def get_data(self) -> perception_packet_pb2.PerceptionPacket:
        if not self._initialized:
            raise RuntimeError("MockCamera not initialized")

        width = 0
        height = 0
        if self._config.HasField("opencv_config"):
            width = int(self._config.opencv_config.width)
            height = int(self._config.opencv_config.height)

        if width <= 0:
            width = 64
        if height <= 0:
            height = 48

        packet = perception_packet_pb2.PerceptionPacket()
        packet.perception_id = self.get_id()
        packet.timestamp_ns = time.time_ns()

        image = packet.image
        image.width = width
        image.height = height
        image.channels = 3
        image.encoding = "bgr8"

        # Solid-color BGR test pattern (blue-ish)
        pixel = bytes([255, 0, 0])
        image.data = pixel * (width * height)
        return packet

    def teardown(self) -> None:
        self._initialized = False
