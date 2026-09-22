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

JointState command topics can have any name because the message itself identifies
the actuator. It is a **position-only** command contract: name/position lengths
must match, the target name must occur exactly once, and velocity/effort must be
empty. `normalized` is rejected. Publisher names come from `sensor_name`.
Each sensor publishes independently; use an aggregator when a consumer requires
one synchronized multi-joint snapshot.

JointState conversions follow the existing device contracts: STS3215 ticks use
4096 counts/revolution, and stepper driver positions use degrees. Feedback from
the STS3215 position sensor is converted to radians; commands are converted back
to the selected driver's units. The TI demo has no supported JointState unit
contract and is rejected. This expresses the device's existing zero reference;
it does not infer URDF offsets, direction, or additional mechanical calibration.
The [STS3215 specifications](https://www.feetechrc.com/products.html?keyword=STS3215)
define encoder resolution; [JointState](https://docs.ros2.org/foxy/api/sensor_msgs/msg/JointState.html)
defines radian/metre units.

Hardware and downstream constraints
----------------------------------

Config validation rejects unsupported wire/driver combinations before hardware
initialization. Conversions reject non-finite values, overflow, underflow to
zero, fractional integer feedback, and integer commands that lose precision
when converted to the internal float API. Float64 input still rounds to float;
a wider ROS type does not add sensor or driver precision. Driver limits and
board-level range checks continue to apply. Teardown runs once per actuator,
even when it has several subscriptions.

| Boundary | Mitigation / next step |
|---|---|
| Internal packets and drivers use float | Keep checked conversions in the existing parser. Widening precision requires an end-to-end packet/driver/firmware change. |
| Hardware units and register widths vary | Retain device validation and native quantization. Add fixed conversion/capability support with each new driver; reject unknown JointState units. |
| STS3215/stepper torque is enable/disable | Never interpret JointState effort as physical torque; Bool is restricted to binary enable gates. |
| Inference/trajectory still produce FLOAT32; scalar observation decoding assumes FLOAT32 | Keep their existing endpoints; expose another typed endpoint on a different topic for external ROS consumers. Update those producers/codecs in a separate change. |
| Dataset message types/shapes can change | Update dataset/model expectations and keep topic types stable during recording. |
| Multiple command sources can target a device | Coordinate command ownership externally; message-type support does not add arbitration. |

Tests cover typed pub/sub, conversion failures, named-joint selection and units,
and both nodes with in-memory mock boards. No hardware is required.
