import unittest

from google.protobuf import descriptor

from robot.action.proto import action_packet_pb2
from robot.perception.proto import perception_packet_pb2
from ros2.utils.packet_parser import (
    _PERCEPTION_REQUIRE_FIELD_BY_NAME,
    ACTION_POSITION_FIELD_PATHS,
    ACTION_SCALAR_ONEOF_FIELDS,
    ACTION_TOPIC_SUFFIX_TO_FIELD,
    PERCEPTION_DATA_TYPE_FIELDS,
    PacketParseError,
    action_packet_from_float,
    denormalize_action_packet,
    denormalize_position_value,
    device_id_from_topic,
    extract_position_from_action,
    extract_scalar_from_action,
    parse_action_type_from_topic,
    require_perception_position,
)

_TYPE_FLOAT = descriptor.FieldDescriptor.TYPE_FLOAT


def _discover_action_type_float_fields() -> set[str]:
    oneof = action_packet_pb2.ActionPacket.DESCRIPTOR.oneofs_by_name["action_type"]
    return {field.name for field in oneof.fields if field.type == _TYPE_FLOAT}


def _discover_action_position_field_paths_from_proto() -> set[str]:
    paths: set[str] = set()
    oneof = action_packet_pb2.ActionPacket.DESCRIPTOR.oneofs_by_name["action_type"]
    for field in oneof.fields:
        if field.type == _TYPE_FLOAT and "position" in field.name:
            paths.add(field.name)
        if field.message_type and field.message_type.name == "ComplexAction":
            for nested in field.message_type.fields:
                if nested.type == _TYPE_FLOAT and "position" in nested.name:
                    paths.add(f"complex.{nested.name}")
    return paths


def _discover_perception_data_type_fields_from_proto() -> set[str]:
    oneof = perception_packet_pb2.PerceptionPacket.DESCRIPTOR.oneofs_by_name[
        "data_type"
    ]
    return {field.name for field in oneof.fields}


def _make_action_with_position_path(
    path: str, *, normalized: bool, value: float
) -> action_packet_pb2.ActionPacket:
    packet = action_packet_pb2.ActionPacket()
    packet.normalized = normalized
    if path == "position":
        packet.position = value
    elif path == "complex.position":
        packet.complex.position = value
    else:
        raise ValueError(f"Unknown position path for test helper: {path}")
    return packet


class PacketParserTest(unittest.TestCase):
    def test_denormalize_top_level_position(self):
        packet = action_packet_pb2.ActionPacket()
        packet.normalized = True
        packet.position = 0.0
        denormalize_action_packet(packet, 100.0, 200.0)
        self.assertAlmostEqual(packet.position, 150.0)

    def test_denormalize_complex_position(self):
        packet = action_packet_pb2.ActionPacket()
        packet.normalized = True
        packet.complex.position = -1.0
        denormalize_action_packet(packet, 100.0, 200.0)
        self.assertAlmostEqual(packet.complex.position, 100.0)

    def test_extract_position_from_action(self):
        packet = action_packet_pb2.ActionPacket()
        packet.complex.position = 12.5
        self.assertAlmostEqual(extract_position_from_action(packet), 12.5)

    def test_extract_scalar_from_action(self):
        packet = action_packet_pb2.ActionPacket()
        packet.speed = 3.5
        self.assertAlmostEqual(extract_scalar_from_action(packet), 3.5)

    def test_require_perception_position(self):
        packet = perception_packet_pb2.PerceptionPacket()
        packet.position.position = 7.5
        self.assertAlmostEqual(require_perception_position(packet), 7.5)

    def test_require_perception_position_missing(self):
        packet = perception_packet_pb2.PerceptionPacket()
        with self.assertRaises(PacketParseError):
            require_perception_position(packet)

    def test_denormalize_position_value_clamps(self):
        self.assertAlmostEqual(denormalize_position_value(2.0, 0.0, 10.0), 10.0)

    def test_parse_action_type_from_topic(self):
        self.assertEqual(
            parse_action_type_from_topic("sts_motor_1/position"), "position"
        )
        self.assertEqual(parse_action_type_from_topic("/servo/dc"), "dc")

    def test_parse_action_type_from_topic_rejects_unknown(self):
        with self.assertRaises(PacketParseError):
            parse_action_type_from_topic("sts_motor_1/unknown")

    def test_device_id_from_topic(self):
        self.assertEqual(device_id_from_topic("sts_motor_1/position"), "sts_motor_1")
        self.assertEqual(
            device_id_from_topic("/so100/sts_motor_1/position"), "sts_motor_1"
        )

    def test_action_packet_from_float_position_normalized(self):
        packet = action_packet_from_float(0.0, "arm/position", normalized=True)
        self.assertTrue(packet.normalized)
        self.assertAlmostEqual(packet.position, 0.0)

    def test_action_packet_from_float_dc(self):
        packet = action_packet_from_float(10.0, "motor/dc")
        self.assertAlmostEqual(packet.dc, 10.0)
        self.assertFalse(packet.normalized)


class PacketParserProtoContractTest(unittest.TestCase):
    """Fail when proto schema and parser registries drift apart."""

    def test_action_position_registry_matches_proto(self):
        self.assertEqual(
            set(ACTION_POSITION_FIELD_PATHS),
            _discover_action_position_field_paths_from_proto(),
        )

    def test_action_scalar_registry_matches_proto(self):
        self.assertEqual(
            set(ACTION_SCALAR_ONEOF_FIELDS),
            _discover_action_type_float_fields(),
        )

    def test_action_topic_suffix_registry_matches_scalar_registry(self):
        """Topic suffix allowlist must cover every scalar wire command."""
        self.assertEqual(
            set(ACTION_TOPIC_SUFFIX_TO_FIELD.keys()),
            set(ACTION_SCALAR_ONEOF_FIELDS),
        )
        for suffix, field in ACTION_TOPIC_SUFFIX_TO_FIELD.items():
            self.assertEqual(suffix, field)

    def test_perception_data_type_registry_matches_proto(self):
        self.assertEqual(
            set(PERCEPTION_DATA_TYPE_FIELDS),
            _discover_perception_data_type_fields_from_proto(),
        )

    def test_perception_require_helpers_cover_registry(self):
        self.assertEqual(
            set(_PERCEPTION_REQUIRE_FIELD_BY_NAME.keys()),
            set(PERCEPTION_DATA_TYPE_FIELDS),
        )

    def test_each_action_position_path_is_denormalized(self):
        for path in ACTION_POSITION_FIELD_PATHS:
            with self.subTest(path=path):
                packet = _make_action_with_position_path(
                    path, normalized=True, value=0.0
                )
                denormalize_action_packet(packet, 0.0, 100.0)
                self.assertAlmostEqual(extract_position_from_action(packet), 50.0)


if __name__ == "__main__":
    unittest.main()
