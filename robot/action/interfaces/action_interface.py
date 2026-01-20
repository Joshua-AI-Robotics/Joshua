from __future__ import annotations

from abc import ABC, abstractmethod

from robot.action.proto import action_packet_pb2


class ActionInterface(ABC):
    """Abstract action interface - high-level interface for all action components."""

    @abstractmethod
    def init(self) -> None:
        raise NotImplementedError

    @abstractmethod
    def get_id(self) -> str:
        raise NotImplementedError

    @abstractmethod
    def set_action(self, action_packet: action_packet_pb2.ActionPacket) -> None:
        raise NotImplementedError

    @abstractmethod
    def teardown(self) -> None:
        raise NotImplementedError
