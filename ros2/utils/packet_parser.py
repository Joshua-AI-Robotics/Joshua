"""Centralized ActionPacket and PerceptionPacket field-access helpers.

See ros2/utils/packet_parser.md for usage and proto change checklist.
"""

from __future__ import annotations

import math
from typing import Optional

from robot.action.proto import action_packet_pb2
from robot.perception.proto import perception_packet_pb2

# Numeric topic aliases for the unified motion payload.
ACTION_POSITION_FIELD_PATHS = ("joint.position",)
ACTION_JOINT_FIELDS = ("position", "velocity", "effort")
ACTION_TOPIC_SUFFIX_TO_FIELD = {
    "position": "position",
    "speed": "velocity",
    "velocity": "velocity",
    "torque": "effort",
    "effort": "effort",
    "dc": "effort",
}
PERCEPTION_DATA_TYPE_FIELDS = (
    "image",
    "position",
    "sensor",
    "point_cloud",
)


class PacketParseError(ValueError):
    """Raised when topic parsing or required proto fields are missing."""


def parse_action_type_from_topic(topic: str) -> str:
    """Return JointCommand field name from /<device_id>/<action_type> topic."""
    suffix = topic.rstrip("/").rsplit("/", 1)[-1].lower()
    field = ACTION_TOPIC_SUFFIX_TO_FIELD.get(suffix)
    if field is None:
        allowed = ", ".join(sorted(ACTION_TOPIC_SUFFIX_TO_FIELD))
        raise PacketParseError(
            f"Unknown action_type '{suffix}' in topic '{topic}' "
            f"(expected suffix: {allowed})"
        )
    return field


def device_id_from_topic(topic: str) -> str:
    """Return device_id segment from /<device_id>/<action_type> topic."""
    parts = topic.strip("/").split("/")
    if len(parts) < 2:
        raise PacketParseError(
            f"Actuator topic '{topic}' must be /<device_id>/<action_type>"
        )
    return parts[-2]


def action_packet_from_float(
    value: float,
    topic: str,
    *,
    position_encoding: int = action_packet_pb2.JointCommand.POSITION_NATIVE,
) -> action_packet_pb2.ActionPacket:
    """Build ActionPacket from a Float32 actuator command topic and value."""
    field = parse_action_type_from_topic(topic)
    if not math.isfinite(value):
        raise PacketParseError("Command must be finite")
    if (
        position_encoding
        not in action_packet_pb2.JointCommand.PositionEncoding.values()
    ):
        raise PacketParseError("Unknown position encoding")
    if (
        position_encoding != action_packet_pb2.JointCommand.POSITION_NATIVE
        and field != "position"
    ):
        raise PacketParseError("Position encoding applies only to position topics")
    packet = action_packet_pb2.ActionPacket()
    packet.joint.joint_name = device_id_from_topic(topic)
    if field == "position":
        packet.joint.position_encoding = position_encoding
    setattr(packet.joint, field, float(value))
    return packet


def map_normalized_position(value: float, lower: float, upper: float) -> float:
    """Map normalized [-1, 1] to raw ticks in [lower, upper]."""
    normalized = max(-1.0, min(1.0, float(value)))
    return lower + (normalized + 1.0) * (upper - lower) / 2.0


def denormalize_position_value(value: float, lower: float, upper: float) -> float:
    """Inference adapter's legacy [-1, 1] clamp policy, not packet decoding."""
    position = map_normalized_position(value, lower, upper)
    return max(lower, min(upper, position))


def resolve_position_encoding(
    packet: action_packet_pb2.ActionPacket,
    lower: float,
    upper: float,
) -> action_packet_pb2.ActionPacket:
    if not packet.HasField("joint"):
        return packet
    joint = packet.joint
    encoding = joint.position_encoding
    if encoding not in action_packet_pb2.JointCommand.PositionEncoding.values():
        raise PacketParseError("Unknown position encoding")
    if joint.HasField("position") and not math.isfinite(joint.position):
        raise PacketParseError("Position must be finite")
    if encoding in (
        action_packet_pb2.JointCommand.POSITION_NATIVE,
        action_packet_pb2.JointCommand.POSITION_SI,
    ):
        return packet
    if not joint.HasField("position"):
        raise PacketParseError("Normalized encoding requires position")
    if not math.isfinite(lower) or not math.isfinite(upper) or lower >= upper:
        raise PacketParseError(
            "Normalization requires finite increasing operational limits"
        )
    minimum = (
        0.0
        if encoding == action_packet_pb2.JointCommand.POSITION_NORMALIZED_ZERO_ONE
        else -1.0
    )
    if not minimum <= joint.position <= 1.0:
        raise PacketParseError("Position outside configured normalized range")
    fraction = (joint.position - minimum) / (1.0 - minimum)
    value = (1.0 - fraction) * lower + fraction * upper
    if not math.isfinite(value):
        raise PacketParseError("Converted position must be finite")
    joint.position = value
    joint.position_encoding = action_packet_pb2.JointCommand.POSITION_NATIVE
    return packet


def extract_position_from_action(packet: action_packet_pb2.ActionPacket) -> float:
    if packet.HasField("joint") and packet.joint.HasField("position"):
        return float(packet.joint.position)
    raise PacketParseError("ActionPacket has no joint.position")


def extract_scalar_from_action(
    packet: action_packet_pb2.ActionPacket,
    topic: Optional[str] = None,
) -> Optional[float]:
    # A Float32 topic cannot carry a multi-field command or SI metadata.
    if (
        not packet.HasField("joint")
        or packet.joint.units != action_packet_pb2.JointCommand.NATIVE
        or packet.joint.position_encoding
        != action_packet_pb2.JointCommand.POSITION_NATIVE
    ):
        return None
    fields = [field for field in ACTION_JOINT_FIELDS if packet.joint.HasField(field)]
    if len(fields) != 1:
        return None
    if topic is not None:
        if (
            parse_action_type_from_topic(topic) != fields[0]
            or device_id_from_topic(topic) != packet.joint.joint_name
        ):
            return None
    value = float(getattr(packet.joint, fields[0]))
    return value if math.isfinite(value) else None


def require_perception_position(
    packet: perception_packet_pb2.PerceptionPacket,
) -> float:
    if not packet.HasField("position"):
        raise PacketParseError("PerceptionPacket has no position field")
    return float(packet.position.position)


def require_perception_image(
    packet: perception_packet_pb2.PerceptionPacket,
) -> perception_packet_pb2.ImageData:
    if not packet.HasField("image"):
        raise PacketParseError("PerceptionPacket has no image field")
    return packet.image


def require_perception_point_cloud(
    packet: perception_packet_pb2.PerceptionPacket,
) -> perception_packet_pb2.PointCloudData:
    if not packet.HasField("point_cloud"):
        raise PacketParseError("PerceptionPacket has no point_cloud field")
    return packet.point_cloud


def require_perception_sensor(
    packet: perception_packet_pb2.PerceptionPacket,
) -> perception_packet_pb2.SensorData:
    if not packet.HasField("sensor"):
        raise PacketParseError("PerceptionPacket has no sensor field")
    return packet.sensor


_PERCEPTION_REQUIRE_FIELD_BY_NAME = {
    "image": require_perception_image,
    "position": require_perception_position,
    "sensor": require_perception_sensor,
    "point_cloud": require_perception_point_cloud,
}
