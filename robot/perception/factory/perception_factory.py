from __future__ import annotations

from robot.perception.camera.mock_camera import MockCamera
from robot.perception.encoder.mock_encoder import MockEncoder
from robot.perception.interfaces.perception_interface import PerceptionInterface
from robot.perception.lidar.mock_lidar import MockLidar
from robot.perception.proto import perception_pb2


def create_perception(
    single_perception: perception_pb2.SinglePerception,
) -> PerceptionInterface:
    # Last Added: MockCamera.
    if single_perception.perception_type == perception_pb2.PerceptionType.CAMERA:
        camera = MockCamera(single_perception.camera)
        camera.init()
        return camera

    # Last Added: MockEncoder.
    if single_perception.perception_type == perception_pb2.PerceptionType.ENCODER:
        encoder = MockEncoder(single_perception.encoder)
        encoder.init()
        return encoder

    # Last Added: MockLidar.
    if single_perception.perception_type == perception_pb2.PerceptionType.LIDAR:
        lidar = MockLidar(single_perception.lidar)
        lidar.init()
        return lidar

    raise NotImplementedError(
        "Python perception drivers are not implemented yet; provide drivers first"
    )
