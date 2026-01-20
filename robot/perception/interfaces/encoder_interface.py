from __future__ import annotations

from robot.perception.interfaces.perception_interface import PerceptionInterface


class EncoderInterface(PerceptionInterface):
    """Abstract encoder interface."""

    def get_position(self) -> float:
        raise NotImplementedError
