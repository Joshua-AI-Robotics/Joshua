from __future__ import annotations

import time

from robot.perception.interfaces.encoder_interface import EncoderInterface
from robot.perception.proto import perception_packet_pb2, perception_pb2


class MockEncoder(EncoderInterface):
    """Mock encoder for Python pipeline testing."""

    def __init__(self, encoder_config: perception_pb2.Encoder) -> None:
        self._config = encoder_config
        self._initialized = False

    def init(self) -> None:
        self._initialized = True

    def get_id(self) -> str:
        return f"mock_encoder_{self._config.id}"

    def get_data(self) -> perception_packet_pb2.PerceptionPacket:
        if not self._initialized:
            raise RuntimeError("MockEncoder not initialized")

        packet = perception_packet_pb2.PerceptionPacket()
        packet.perception_id = self.get_id()
        packet.timestamp_ns = time.time_ns()
        packet.position.position = 0.0
        return packet

    def get_position(self) -> float:
        if not self._initialized:
            raise RuntimeError("MockEncoder not initialized")
        return 0.0

    def teardown(self) -> None:
        self._initialized = False
