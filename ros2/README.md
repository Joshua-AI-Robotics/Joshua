# ROS 2 nodes

Hardware publishers, actuator subscribers, and `trajectory_publisher` run in C++.
Inference is the only Python ROS node. Its required Python support remains:
`node_runner.py`, `image_converter.py`, `ros2_type_resolver.py`, and
`utils/qos_setting.py` / `utils/ros_python_paths.py`.

`ros2_type_resolver.py` maps `Ros2DataType` enums to generated Python message
classes for inference; dataset conversion helpers have been removed. Add new
inference message types to `ROS2_TYPE_MAPPING` and the inference observation
codec as needed.

The data subscriber, data store and dataset inspector, MuJoCo mirror mode,
and Python point-cloud visualizer have been removed. Their config fields and
enum values are reserved; recording and mirror presets must be migrated before
use. The other simulation modes remain available.

## Trajectory playback

`trajectory_publisher.cc` uses the existing `TRAJECTORY_PUBLISHER` node type,
Bazel target `//ros2:trajectory_publisher`, and CLI:
`<node_name> <node_id> <config_path>`.

It merges matching trajectory entries, stably sorts waypoints by timestamp,
waits until every used topic has a subscriber, and repeats the sequence using
steady-clock wall timers. Callbacks return between deadlines so shutdown stays
responsive. Playback follows wall time, including when ROS simulated time is set.

The wire contract remains `std_msgs/msg/Float32`: scalar `position`, `speed`,
`torque`, and `dc` actions supply `data`; topics and QoS come from config.
Waypoint topics without an explicit publisher default to Float32. Subscriber
configuration controls normalization; `ActionPacket.normalized` is not sent
on the wire. Drivers still decide which commands and ranges they accept.
Unsupported message/action types, non-finite values, invalid timestamps, empty
trajectories, and zero-duration loops now fail startup instead of being skipped
or busy-looping. The loop duration is the final waypoint timestamp.

Position publishing
-------------------
`position_publisher.cc` is the standalone node for `POSITION_PUBLISHER` entries.
It uses the `PositionPublishers` component to read sensors and publish their values.
`actuator_subscriber` only receives action commands and executes them; it does not
host sensor publishers. Position sensors and actuators must use separate bus owners
with the current standalone node layout.
The old `encoder_publisher` executable and `ENCODER_PUBLISHER` node type have
been removed; use `POSITION_PUBLISHER` for standalone position sensors. Topic
names and Float32 position values are unchanged.
