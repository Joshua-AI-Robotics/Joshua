"""Centralized ActionPacket and PerceptionPacket parse/field-access helpers.

See ros2/utils/packet_parser.md for usage and proto change checklist.
"""

from __future__ import annotations

from typing import Optional, Tuple

from google.protobuf.message import DecodeError

from robot.action.proto import action_packet_pb2
from robot.perception.proto import perception_packet_pb2

# Registries synced with action_packet.proto / perception_packet.proto.
# Contract tests in packet_parser_test.py fail if proto adds fields not listed here.
ACTION_POSITION_FIELD_PATHS = (
    "position",
    "complex.position",
)
ACTION_SCALAR_ONEOF_FIELDS = (
    "position",
    "speed",
    "torque",
    "dc",
)
PERCEPTION_DATA_TYPE_FIELDS = (
    "image",
    "position",
    "sensor",
    "point_cloud",
)


class PacketParseError(ValueError):
    """Raised when protobuf bytes cannot be parsed or required fields are missing."""


def parse_action_packet(data: bytes) -> action_packet_pb2.ActionPacket:
    packet = action_packet_pb2.ActionPacket()
    try:
        packet.ParseFromString(data)
    except DecodeError as exc:
        raise PacketParseError("Failed to parse ActionPacket") from exc
    return packet


def parse_perception_packet(data: bytes) -> perception_packet_pb2.PerceptionPacket:
    packet = perception_packet_pb2.PerceptionPacket()
    try:
        packet.ParseFromString(data)
    except DecodeError as exc:
        raise PacketParseError("Failed to parse PerceptionPacket") from exc
    return packet


def serialize_action_packet(packet: action_packet_pb2.ActionPacket) -> bytes:
    return packet.SerializeToString()


def serialize_perception_packet(
    packet: perception_packet_pb2.PerceptionPacket,
) -> bytes:
    return packet.SerializeToString()


def map_normalized_position(value: float, lower: float, upper: float) -> float:
    """Map normalized [-1, 1] to raw ticks in [lower, upper]."""
    normalized = max(-1.0, min(1.0, float(value)))
    return lower + (normalized + 1.0) * (upper - lower) / 2.0


def denormalize_position_value(value: float, lower: float, upper: float) -> float:
    """Map normalized position to raw ticks and clamp to operational limits."""
    position = map_normalized_position(value, lower, upper)
    return max(lower, min(upper, position))


def _apply_to_position_sources(
    packet: action_packet_pb2.ActionPacket,
    transform,
) -> None:
    for path in ACTION_POSITION_FIELD_PATHS:
        if path == "position":
            if packet.WhichOneof("action_type") == "position":
                packet.position = transform(packet.position)
        elif path == "complex.position":
            if packet.WhichOneof(
                "action_type"
            ) == "complex" and packet.complex.HasField("position"):
                packet.complex.position = transform(packet.complex.position)
        else:
            raise ValueError(f"Unhandled ACTION_POSITION_FIELD_PATHS entry: {path}")


def denormalize_action_packet(
    packet: action_packet_pb2.ActionPacket,
    lower: float,
    upper: float,
) -> action_packet_pb2.ActionPacket:
    if not packet.normalized:
        return packet

    def denorm(value: float) -> float:
        return denormalize_position_value(value, lower, upper)

    _apply_to_position_sources(packet, denorm)
    return packet


def extract_position_from_action(packet: action_packet_pb2.ActionPacket) -> float:
    for path in ACTION_POSITION_FIELD_PATHS:
        if path == "position" and packet.WhichOneof("action_type") == "position":
            return float(packet.position)
        if (
            path == "complex.position"
            and packet.WhichOneof("action_type") == "complex"
            and packet.complex.HasField("position")
        ):
            return float(packet.complex.position)
    raise PacketParseError(
        "ActionPacket has no position field "
        f"(expected one of: {', '.join(ACTION_POSITION_FIELD_PATHS)})"
    )


def extract_scalar_from_action(
    packet: action_packet_pb2.ActionPacket,
) -> Optional[float]:
    which = packet.WhichOneof("action_type")
    if which not in ACTION_SCALAR_ONEOF_FIELDS:
        return None
    return float(getattr(packet, which))


def action_to_perception_position_packet(
    action: action_packet_pb2.ActionPacket,
    *,
    limits: Optional[Tuple[float, float]] = None,
) -> perception_packet_pb2.PerceptionPacket:
    position = extract_position_from_action(action)
    if action.normalized:
        if limits is None:
            raise PacketParseError(
                "normalized ActionPacket requires limits for PerceptionPacket conversion"
            )
        position = denormalize_position_value(position, limits[0], limits[1])

    perception = perception_packet_pb2.PerceptionPacket()
    perception.position.position = float(position)
    if action.timestamp_ns != 0:
        perception.timestamp_ns = action.timestamp_ns
    return perception


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


def perception_data_kind(
    packet: perception_packet_pb2.PerceptionPacket,
) -> Optional[str]:
    return packet.WhichOneof("data_type")
