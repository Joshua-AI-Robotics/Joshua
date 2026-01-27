from __future__ import annotations

from robot.perception.camera.mock_camera import MockCamera
from robot.perception.encoder.mock_encoder import MockEncoder
from robot.perception.interfaces.perception_interface import PerceptionInterface
from robot.perception.proto import perception_pb2


def create_perception(
    single_perception: perception_pb2.SinglePerception,
) -> PerceptionInterface:
    # TODO(hmoon): Add camera factory here.
    # Last Added: MockCamera.
    if single_perception.perception_type == perception_pb2.PerceptionType.CAMERA:
        camera = MockCamera(single_perception.camera)
        camera.init()
        return camera

    # TODO(hmoon): Add encoder factory here.
    # Last Added: MockEncoder.
    if single_perception.perception_type == perception_pb2.PerceptionType.ENCODER:
        encoder = MockEncoder(single_perception.encoder)
        encoder.init()
        return encoder

    raise NotImplementedError(
        "Python perception drivers are not implemented yet; provide drivers first"
    )
