ROS 2 Type Resolver Utilities  
==============================

Overview
--------
`ros2/ros2_type_resolver.py` centralizes ROS 2 type handling for the codebase.
It provides:
- Type resolution from enum or string to Python classes
- Canonical type-string extraction from message instances
- A reverse lookup to a simple mapping key (e.g., "IMAGE", "IMU")
- Helpers to add canonical fields to rows during post-processing
- A high-level `build_entry_for_message` that constructs one dataset row

Main APIs
---------
- `resolve_message_class(ros2_type: str, enum_value: int) -> Any`
- `resolve_message_class_from_enum(enum_value: int) -> Any`
- `get_ros2_type_string_from_enum(enum_value: int) -> str`
- `get_ros2_type_name(msg: Any) -> str`
- `get_ros2_mapping_key(ros2_type: str) -> str`
  - Reverse lookup from fully-qualified type (e.g., `sensor_msgs/msg/Image`) to key (e.g., `"IMAGE"`)
- `add_post_process_feature(base_entry: dict, ros2_type: str, value) -> dict`
  - Applies canonical field naming (e.g., IMAGE/COMPRESSED_IMAGE -> `image`)
- `build_entry_for_message(base_entry: dict, ros2_type: str, msg, bridge=None) -> dict`
  - Single entry construction with fast paths for images and a generic fallback

Mapping
-------
`ROS2_TYPE_MAPPING` defines supported types and their canonical keys. The reverse mapping powers `get_ros2_mapping_key` to drive fast-path logic for known types.

Fast paths
----------
- Images (`sensor_msgs/msg/Image`, `sensor_msgs/msg/CompressedImage`):
  - Decoded with a lazily managed `CvBridge` instance
  - Added to the row as `image` (numpy)
- Generic types:
  - Converted via `rosidl_runtime_py.convert.message_to_ordereddict` and merged into the row

Extend the resolver
-------------------
1) Add new types to `ROS2_TYPE_MAPPING` if missing.  
2) Extend `add_post_process_feature` to produce canonical field names for those types (e.g., map all scalar std_msgs to a single `"value"` field).  
3) Extend `build_entry_for_message` to add a specialized path (decode/transform) before falling back to the generic dict conversion.

Usage with DataStore
--------------------
`ai/train/data_store.py` calls:
```python
row = build_entry_for_message(base_entry, msg_type, msg)
```
- `base_entry` supplies stable metadata (`topic`, `timestamp`)
- `msg_type` is the fully-qualified ROS 2 type string
- The resolver returns the final row with any type-specific fields

Notes
-----
- The resolver assumes a single consistent type per topic (ROS 2 convention).
- It is safe to call from streaming/generator code paths; `CvBridge` is instantiated lazily and reused.

Position publishing
-------------------
`position_publisher.cc` is the standalone node for `POSITION_PUBLISHER` entries.
It reads sensors and publishes their values using the configured message type.
`actuator_subscriber` only receives action commands and executes them; it does not
host sensor publishers. Position sensors and actuators must use separate bus owners
with the current standalone node layout.
The old `encoder_publisher` executable and `ENCODER_PUBLISHER` node type have
been removed; use `POSITION_PUBLISHER` for standalone position sensors. Topic
names and Float32 position values are unchanged.

Typed position and actuator messages
------------------------------------

The nodes select **compiled C++ message types** using `ros2_data_type`. Fixed
conversions and the typed publisher/subscriber factories live in
`utils/packet_parser.cc`. There are no field paths, runtime introspection,
message templates, or changes to the config schema. `position_publisher.cc`
contains the complete node, with no separate position-publisher header.

| Message type | Fixed contract |
|---|---|
| FLOAT32, FLOAT64, signed/unsigned 8/16/32/64-bit integers, BYTE, CHAR | `data` contains one native-unit value. |
| Corresponding numeric MULTI_ARRAY types | Exactly one contiguous value in `data`; no joint-index inference. |
| JOINT_STATE | Publisher emits one named joint's position in radians and a ROS timestamp. Subscriber selects `actuator_name` from `name` regardless of array order. |
| BOOL | Subscription only: torque-enable gate for STS3215 or stepper drivers. |
| Other types (Image, Pose, String, etc.) | Rejected: no defined position/command conversion. |

For a different scalar wire type, change only the enum:

```text
publishers {
  ros2_data_type: FLOAT64
  topic: "elbow/position"
  publish_rate_hz: 30
}
subscriptions {
  ros2_data_type: FLOAT64
  topic: "elbow/position"
}
```

Scalar/array command topics retain `/<actuator_name>/position`, `/speed`, or
`/torque`. Current drivers do not implement `/dc`; validation rejects it.
`normalized: true` retains the existing [-1, 1] scalar/array position behavior.
Both endpoints must use the same wire type; Float32 and Float64 do not match.

JointState command topics use `config.topic()` directly and can have any valid ROS
topic name. The message's `name` array selects `actuator_name`; no synthetic
`/position` topic is constructed. Names must be nonempty and unique. Each numeric
array must be empty or match the names array, all supplied values must be finite,
and at least one of position, velocity, or effort must be present.

The decoder emits `ActionPacket.joint_command`, retaining optional double-valued
position, velocity, and effort in SI units (rad, rad/s, N*m for revolute joints;
m, m/s, N for prismatic joints). An omitted value is distinct from zero. It also
preserves the selected joint name, `header.frame_id`, and the source timestamp in
`ActionPacket.timestamp_ns`. Nanoseconds must be below 1e9; zero and negative
source times are preserved. Metadata does not schedule execution, enforce
freshness, identify a clock domain, or trigger coordinate transforms. Those
policies belong to future clock/PTP and coordinate-handling work.

Drivers own command capabilities and SI-to-native conversion. Current STS3215
and stepper drivers support **position-only execution**: STS3215 converts radians
to ticks (4096/revolution), and stepper converts radians to degrees before its
existing steps conversion. Both reject any supplied velocity/effort, including
zero, before applying the position. Their legacy speed setting is nonnegative;
legacy torque is an enable gate, not physical effort. The TI demo rejects all
joint commands because it has no physical SI contract. Decoding succeeds
independently of those driver restrictions, so future drivers can implement the
new packet without changes to subscription creation. Validate every requested
field, combination, unit conversion and limit before issuing writes; this is
prevalidation, not a guarantee of atomic hardware execution.

`normalized` is rejected for JointState commands. Existing scalar/array commands
retain their native-unit and normalization conventions. The new packet arm must
be understood by downstream drivers; rebuild packet consumers together. The ROS
message and config schemas are unchanged.

Position feedback still publishes one named position per sensor (using
`sensor_name`), converted to radians with a ROS timestamp. It does not fabricate
velocity or effort. Use an aggregator for synchronized multi-joint feedback.
Device zero references are retained; URDF offsets and mechanical calibration
are not inferred.

Hardware and downstream constraints
----------------------------------

The shared C++ runner (`RunNode<T>` in `node_runner.h`) loads and validates the
full config before constructing a node, including when launched directly without
`node_generator`. Position and actuator constructors rely on that shared check.
Config validation rejects invalid subscription settings before hardware
initialization. JointState field and driver capability checks run on each command.
Conversions reject non-finite values, overflow, underflow to zero, fractional integer feedback, and integer commands that lose precision
when converted to the internal float API. Float64 input still rounds to float;
a wider ROS type does not add sensor or driver precision. Driver limits and
board-level range checks continue to apply. Teardown runs once per actuator,
even when it has several subscriptions.

| Boundary | Mitigation / next step |
|---|---|
| Legacy packets and drivers use float | Scalars retain checked conversion to float. JointCommand preserves double precision until checked driver conversion; hardware precision is unchanged. |
| Hardware units and register widths vary | Retain device validation and native quantization. Implement JointCommand conversion and capability checks in each driver; reject unsupported fields before writes. |
| STS3215/stepper torque is enable/disable | Never map JointState physical effort to these gates. Reject effort; Bool remains restricted to enable gates. |
| Inference/trajectory still produce FLOAT32; scalar observation decoding assumes FLOAT32 | Keep their existing endpoints; expose another typed endpoint on a different topic for external ROS consumers. Update those producers/codecs in a separate change. |
| Dataset message types/shapes can change | Update dataset/model expectations and keep topic types stable during recording. |
| Multiple command sources can target a device | Coordinate command ownership externally; message-type support does not add arbitration. |

Tests cover typed pub/sub, conversion failures, named-joint selection and units,
and shared runner validation. No hardware is required.
