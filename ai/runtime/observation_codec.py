"""Decode ROS2 messages into canonical, ROS-free observation payloads.

This is the host-side translation layer. It keeps ROS message types out
of model adapters: IMAGE messages become numpy arrays (HWC, rgb8) and
FLOAT32 messages become Python floats.
"""

from __future__ import annotations

from typing import Any

from ai.runtime.types import ChannelRole
from ros2.image_converter import ImageConverter
from ros2.proto import ros2_data_type_pb2


class ObservationCodec:
    """Maps ``Ros2DataType`` values to roles and decoded payloads."""

    def __init__(self):
        self._image_converter = ImageConverter()

    def role_for(self, ros2_data_type: int) -> ChannelRole:
        if ros2_data_type == ros2_data_type_pb2.Ros2DataType.IMAGE:
            return ChannelRole.IMAGE
        if ros2_data_type == ros2_data_type_pb2.Ros2DataType.FLOAT32:
            return ChannelRole.SCALAR
        return ChannelRole.UNKNOWN

    def decode(self, ros2_data_type: int, msg: Any) -> Any:
        """Decode a ROS message into a canonical payload."""
        if ros2_data_type == ros2_data_type_pb2.Ros2DataType.IMAGE:
            return self._image_converter.imgmsg_to_cv2(msg, desired_encoding="rgb8")
        if ros2_data_type == ros2_data_type_pb2.Ros2DataType.FLOAT32:
            return float(msg.data)
        return msg
