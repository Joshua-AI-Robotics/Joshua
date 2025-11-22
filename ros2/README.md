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


