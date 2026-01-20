from __future__ import annotations

from robot.perception.interfaces.perception_interface import PerceptionInterface
from robot.perception.proto import perception_pb2


def create_perception(
    single_perception: perception_pb2.SinglePerception,
) -> PerceptionInterface:
    raise NotImplementedError(
        "Python perception drivers are not implemented yet; remove or provide drivers first"
    )
