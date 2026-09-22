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
It reads sensors and publishes their values using the configured message mapping.
`actuator_subscriber` only receives action commands and executes them; it does not
host sensor publishers. Position sensors and actuators must use separate bus owners
with the current standalone node layout.
The old `encoder_publisher` executable and `ENCODER_PUBLISHER` node type have
been removed; use `POSITION_PUBLISHER` for standalone position sensors. Topic
names and Float32 position values are unchanged.

Mapped position and actuator messages
------------------------------------

`position_publisher.cc` contains the complete position node; there is no separate
position-publisher header. Both position publishing and actuator subscriptions
use `utils/mapped_message.*` to load ROS C++ type support and compile explicit
field mappings. Every type in `Ros2DataType` is resolved, including nested
geometry, sensor, navigation and TF messages. The selected leaf must be numeric
or boolean. `String` and `Empty` have no numeric payload and cannot carry a
position/command through this adapter. Strings are supported as publisher
metadata constants, not parsed as numeric commands.

Existing `FLOAT32` endpoints default to field `data` and need no config changes.
Every other type requires `scalar_mapping.field_path`. Paths use dotted members
and explicit zero-based indices, such as `linear.x`, `pose.position.x`,
`position[0]`, or `transforms[0].transform.translation.x`. Indices must be below
4096 and within any fixed/bounded ROS array. Subscriptions reject a message if
an indexed element is absent. An index is a literal message-array index;
MultiArray layout offsets and strides are not interpreted automatically.

For example, an external controller may publish a Float64 command on an
arbitrary topic:

```text
subscriptions {
  ros2_data_type: FLOAT64
  topic: "/controller/elbow_target"
  command: "position"
  scalar_mapping { field_path: "data" scale: 57.29577951308232 }
}
```

This example converts radians to degrees **only for a driver configured to use
degrees**. STS3215 currently uses native ticks, so it needs its own calibration,
scale and offset. The mapping always computes `output = input * scale + offset`;
an omitted scale is 1. For subscriptions, output is in the driver's units. For
publishers, output is in the ROS consumer's units.

`command` accepts `position`, `speed`, or `torque`. If omitted, the existing
`/<actuator_name>/<command>` convention remains mandatory. No current motor
driver implements `dc`, so that endpoint is rejected during config validation.
`normalized: true` retains the legacy [-1, 1] position mapping to operational
limits, and cannot be combined with an explicit scale or nonzero offset.

A publisher can populate metadata as well as its selected numeric field:

```text
publishers {
  ros2_data_type: JOINT_STATE
  topic: "/feedback/elbow"
  publish_rate_hz: 30
  scalar_mapping {
    field_path: "position[0]"
    scale: 0.001
    constants { field_path: "name[0]" text: "elbow" }
    constants { field_path: "header.frame_id" text: "base_link" }
  }
}
```

The scale above is illustrative; it must come from the actual encoder
calibration. Constants are publisher-only. Unmapped fields retain the ROS
message defaults, including timestamps. Dynamic arrays grow to contain the
configured elements. This is a field adapter: it does not construct a complete
image, transform tree, valid orientation, or synchronized multi-joint state from
one scalar sensor. Configure required metadata/defaults explicitly, and use a
separate aggregation/conversion node when the consumer requires richer semantics.
JointState subscriptions use configured array indices, so the external publisher
must guarantee ordering. A named-joint selector is a future extension.

Validation and hardware limits
------------------------------

Static config validation checks mapping syntax, numeric scale/offset, command
semantics, rates and conflicting types among the position/actuator endpoints.
At node startup, all of that node's paths and installed type-support libraries
are checked **before creating any driver**. No hardware is opened for these
checks. Nodes do not silently fall back to another wire type. Config validation
alone does not load ROS introspection or inspect the live external graph.

Runtime conversions reject NaN/infinity, overflow, underflow to zero, missing
array elements, fractional integer output, and booleans other than 0/1. Integer
commands that cannot be represented exactly by the internal float API are
rejected. Float64 input is checked and narrowed to float, with ordinary float
rounding; Float64 output cannot restore precision absent from the sensor packet.
Driver errors are reported with their status and operational-limit checks remain
in the drivers. Multiple subscriptions for the same actuator trigger teardown
only once.

Downstream compatibility and mitigation plan
-------------------------------------------

| Boundary | Constraint | Mitigation |
|---|---|---|
| ROS message -> action/perception packet | Internal numeric values remain 32-bit floats | Check conversions; widening end to end would require packet, driver, board and firmware review, not just a ROS type change. |
| Action -> driver | Units, operational limits and supported operations are device-specific | Configure scale/offset and command explicitly; retain driver validation. A future driver capability API should describe modes, units and ranges for richer startup validation. |
| Driver -> board | Native registers may quantize or clamp values; STS3215/stepper torque is an enable gate, not physical effort | Preserve native driver semantics; never infer physical torque from a field named `effort`. Add device-specific range/capability checks alongside future drivers. |
| Position -> inference | The current observation codec treats only FLOAT32 as a scalar observation | Keep a FLOAT32 endpoint for existing models; add mapped output on a different topic for external consumers. Model observation decoding needs a separate migration. |
| Inference/trajectory -> actuator | Existing producers emit FLOAT32 | Keep their FLOAT32 subscription and add a separate mapped subscription/topic for external controllers. Coordinate multiple command sources externally. |
| Data recording | Generic message recording supports structured data, but dataset fields/shapes change | Update dataset/model expectations when changing topic types; keep topic types stable during a recording. |
| Position aggregation | Each sensor publishes independently | Use separate topics or an aggregator for consumers requiring one synchronized multi-joint message. |
| Deployment | Generic adapters dynamically load message type-support libraries | Install the selected ROS message packages in the Docker image, and source ROS so AMENT_PREFIX_PATH resolves them. Custom types would also require a future string-type config field. |

Tests exercise all configured message-type libraries, typed serialization,
metadata, nested arrays, conversion failures, and both nodes using in-memory
mock boards. They do not open hardware. The C++/Python type-name table agreement
is covered by `//ros2/utils:type_mapping_test`.
