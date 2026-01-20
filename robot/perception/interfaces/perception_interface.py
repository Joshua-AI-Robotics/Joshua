from __future__ import annotations

from abc import ABC, abstractmethod

from robot.perception.proto import perception_packet_pb2


class PerceptionInterface(ABC):
    """Abstract perception interface."""

    @abstractmethod
    def init(self) -> None:
        raise NotImplementedError

    @abstractmethod
    def get_id(self) -> str:
        raise NotImplementedError

    @abstractmethod
    def get_data(self) -> perception_packet_pb2.PerceptionPacket:
        raise NotImplementedError

    @abstractmethod
    def teardown(self) -> None:
        raise NotImplementedError
