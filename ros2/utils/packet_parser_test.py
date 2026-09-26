import unittest

from google.protobuf import descriptor

from robot.action.proto import action_packet_pb2
from robot.perception.proto import perception_packet_pb2
from ros2.utils.packet_parser import (
    _PERCEPTION_REQUIRE_FIELD_BY_NAME,
    ACTION_JOINT_FIELDS,
    ACTION_POSITION_FIELD_PATHS,
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

_TYPE_DOUBLE = descriptor.FieldDescriptor.TYPE_DOUBLE


def _discover_action_type_float_fields() -> set[str]:
    return {
        f.name
        for f in action_packet_pb2.JointCommand.DESCRIPTOR.fields
        if f.type == _TYPE_DOUBLE
    }


def _discover_action_position_field_paths_from_proto() -> set[str]:
    return {
        "joint." + f.name
        for f in action_packet_pb2.JointCommand.DESCRIPTOR.fields
        if f.type == _TYPE_DOUBLE and "position" in f.name
    }


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
    if path != "joint.position":
        raise ValueError(f"Unknown position path: {path}")
    packet.joint.position = value
    return packet


class PacketParserTest(unittest.TestCase):
    def test_denormalize_joint_position(self):
        packet = action_packet_pb2.ActionPacket()
        packet.normalized = True
        packet.joint.position = 0.0
        denormalize_action_packet(packet, 100.0, 200.0)
        self.assertAlmostEqual(packet.joint.position, 150.0)

    def test_denormalize_lower_limit(self):
        packet = action_packet_pb2.ActionPacket()
        packet.normalized = True
        packet.joint.position = -1.0
        denormalize_action_packet(packet, 100.0, 200.0)
        self.assertAlmostEqual(packet.joint.position, 100.0)

    def test_extract_position_from_action(self):
        packet = action_packet_pb2.ActionPacket()
        packet.joint.position = 12.5
        self.assertAlmostEqual(extract_position_from_action(packet), 12.5)

    def test_multi_field_joint_cannot_be_extracted_as_scalar(self):
        packet = action_packet_pb2.ActionPacket()
        packet.joint.joint_name = "joint"
        packet.joint.position = 0.5
        packet.joint.velocity = 0.0
        denormalize_action_packet(packet, 100.0, 200.0)
        self.assertEqual(packet.joint.position, 0.5)
        self.assertTrue(packet.joint.HasField("velocity"))
        self.assertFalse(packet.joint.HasField("effort"))
        self.assertIsNone(extract_scalar_from_action(packet))

    def test_extract_scalar_from_action(self):
        packet = action_packet_pb2.ActionPacket()
        packet.joint.velocity = 3.5
        self.assertAlmostEqual(extract_scalar_from_action(packet), 3.5)

    def test_normalization_cleared_and_multi_field_scalar_rejected(self):
        packet = action_packet_from_float(0, "arm/position", normalized=True)
        denormalize_action_packet(packet, 100, 200)
        self.assertFalse(packet.normalized)
        denormalize_action_packet(packet, 100, 200)
        self.assertEqual(packet.joint.position, 150)
        self.assertIsNone(extract_scalar_from_action(packet, "arm/speed"))
        packet.joint.velocity = 0
        self.assertIsNone(extract_scalar_from_action(packet))

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
        self.assertEqual(parse_action_type_from_topic("/servo/dc"), "effort")

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
        self.assertAlmostEqual(packet.joint.position, 0.0)

    def test_action_packet_from_float_dc(self):
        packet = action_packet_from_float(10.0, "motor/dc")
        self.assertAlmostEqual(packet.joint.effort, 10.0)
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
            set(ACTION_JOINT_FIELDS),
            _discover_action_type_float_fields(),
        )

    def test_action_topic_suffix_registry_matches_scalar_registry(self):
        """Topic suffix allowlist must cover every scalar wire command."""
        self.assertEqual(
            set(ACTION_TOPIC_SUFFIX_TO_FIELD.values()),
            set(ACTION_JOINT_FIELDS),
        )

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
