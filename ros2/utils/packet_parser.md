# packet_parser

Centralized helpers for **ActionPacket** and **PerceptionPacket** protobuf semantics at the ROS2 boundary. Use this module instead of scattering `HasField`, `WhichOneof`, and denormalization logic across nodes.

| Language | Library target | Source |
|----------|----------------|--------|
| Python | `//ros2/utils:packet_parser_py` | [`packet_parser.py`](packet_parser.py) |
| C++ | `//ros2/utils:packet_parser` | [`packet_parser.h`](packet_parser.h), [`packet_parser.cc`](packet_parser.cc) |

Proto definitions:

- [`robot/action/proto/action_packet.proto`](../../robot/action/proto/action_packet.proto)
- [`robot/perception/proto/perception_packet.proto`](../../robot/perception/proto/perception_packet.proto)

## What it does

- **Parse / serialize** protobuf bytes on `UInt8MultiArray` topics
- **Denormalize** normalized `ActionPacket` positions using operational limits
- **Extract** position or scalar action fields without duplicating oneof switches
- **Validate** required perception fields before publish (`require_perception_*`)
- **Convert** `ActionPacket` position → `PerceptionPacket` (inference / trajectory)

## Who uses it

| Node | Typical calls |
|------|----------------|
| `actuator_subscriber` | `parse_action_packet`, `denormalize_action_packet` |
| `operational_limit_calibration` | `parse_perception_packet`, `require_perception_position` |
| `inference`, `trajectory_publisher` | `extract_scalar_from_action`, `action_to_perception_position_packet`, `serialize_*` |
| Encoder / camera / lidar publishers | `require_perception_*`, `serialize_perception_packet` |

**Out of scope:** hardware drivers (`sts3215_driver`, `pybricks_driver`, etc.) still consume fully-formed `ActionPacket` after ROS parsing. `payload_type` validation stays in each node config.

## Registries (single source of truth)

Python exports three registries in [`packet_parser.py`](packet_parser.py). Keep them in sync with the C++ implementation when you change behavior.

```python
ACTION_POSITION_FIELD_PATHS   # position-bearing ActionPacket fields
ACTION_SCALAR_ONEOF_FIELDS    # float oneof arms for Float32 publish
PERCEPTION_DATA_TYPE_FIELDS   # PerceptionPacket.data_type oneof arms
```

Contract tests in [`packet_parser_test.py`](packet_parser_test.py) compare these registries to the generated proto descriptors. CI fails if proto and parser drift apart.

Run tests:

```bash
bazel test //ros2/utils:packet_parser_test
```

---

## Action items when changing protos

Use this checklist any time you edit `action_packet.proto` or `perception_packet.proto`.

### 1. After editing `action_packet.proto`

| Change | Action |
|--------|--------|
| New **float** field on `action_type` oneof (e.g. `acceleration`) | Add field name to `ACTION_SCALAR_ONEOF_FIELDS` in **Python and C++**. Update `ExtractScalarFromAction` in C++ if not registry-driven yet. |
| New **position** field (name contains `position`) on `action_type` or inside `ComplexAction` | Add path to `ACTION_POSITION_FIELD_PATHS` (e.g. `"complex.staging_position"`). Implement handling in `_apply_to_position_sources`, `extract_position_from_action`, and C++ `ApplyToPositionSources` / `ExtractPositionFromAction`. |
| New position field **without** `position` in the name | Add path to `ACTION_POSITION_FIELD_PATHS` manually **and** extend `_discover_action_position_field_paths_from_proto()` in the test file so the contract test covers it. |
| Field that should denormalize when `normalized=true` | Must be listed in `ACTION_POSITION_FIELD_PATHS`. |
| Non-float oneof arm (e.g. new preset) | No scalar registry change. Update driver logic if actuators must handle it. |

**Verify:**

```bash
bazel test //ros2/utils:packet_parser_test
```

Expected failing tests if you forget a registry update:

- `PacketParserProtoContractTest.test_action_scalar_registry_matches_proto`
- `PacketParserProtoContractTest.test_action_position_registry_matches_proto`
- `PacketParserProtoContractTest.test_each_action_position_path_is_denormalized`

### 2. After editing `perception_packet.proto`

| Change | Action |
|--------|--------|
| New arm on `data_type` oneof (e.g. `imu`) | 1. Add name to `PERCEPTION_DATA_TYPE_FIELDS` (Python). 2. Add `require_perception_*` helper. 3. Register it in `_PERCEPTION_REQUIRE_FIELD_BY_NAME`. 4. Mirror validation in C++ (`RequirePerception*` or `absl::Status` checker). 5. Wire the relevant **publisher** node to call the new helper before publish. |
| New optional field inside existing message (e.g. `velocity` on `PositionData`) | Parser `require_*` helpers may still pass. Update consumers that need the new field; add behavioral tests if extraction logic changes. |

**Verify:**

```bash
bazel test //ros2/utils:packet_parser_test
```

Expected failing tests if you forget:

- `test_perception_data_type_registry_matches_proto`
- `test_perception_require_helpers_cover_registry`

### 3. C++ parity

Python has contract tests; C++ does not yet. When you change Python registries or helpers, update the matching functions in [`packet_parser.cc`](packet_parser.cc) in the same PR.

### 4. ROS nodes (only if wire format changes)

Registry/parser updates alone are not enough if a **new perception type** needs publishing:

- Add publisher validation (`require_perception_*`)
- Update configs (`payload_type`, `ros2_data_type`) if needed
- Do **not** re-add perception parsing to `actuator_subscriber` unless product requirements change

---

## Example: adding `complex.staging_position`

1. Add to `action_packet.proto`:

   ```protobuf
   optional float staging_position = 6;  // inside ComplexAction
   ```

2. Update `ACTION_POSITION_FIELD_PATHS`:

   ```python
   ACTION_POSITION_FIELD_PATHS = (
       "position",
       "complex.position",
       "complex.staging_position",
   )
   ```

3. Implement read/write for `"complex.staging_position"` in `_apply_to_position_sources` and `extract_position_from_action` (and C++).

4. Run `bazel test //ros2/utils:packet_parser_test` — contract tests should pass once registries and handlers match proto.

---

## Error handling

| Python | C++ |
|--------|-----|
| Raises `PacketParseError` | Returns `absl::Status` / `absl::StatusOr<T>` |

Callers (ROS nodes) should log and skip the message on parse/validation failure rather than crashing the node.
