from __future__ import annotations

from robot.perception.camera.mock_camera import MockCamera
from robot.perception.encoder.mock_encoder import MockEncoder
from robot.perception.interfaces.perception_interface import PerceptionInterface
from robot.perception.lidar.mock_lidar import MockLidar
from robot.perception.proto import perception_pb2


def create_perception(
    single_perception: perception_pb2.SinglePerception,
) -> PerceptionInterface:
    if single_perception.perception_type == perception_pb2.PerceptionType.CAMERA:
        camera = MockCamera(single_perception.camera)
        camera.init()
        return camera

    if single_perception.perception_type == perception_pb2.PerceptionType.ENCODER:
        encoder = MockEncoder(single_perception.encoder)
        encoder.init()
        return encoder

    if single_perception.perception_type == perception_pb2.PerceptionType.LIDAR:
        lidar = MockLidar(single_perception.lidar)
        lidar.init()
        return lidar

    raise NotImplementedError(
        f"Unsupported perception type: {single_perception.perception_type}"
    )
