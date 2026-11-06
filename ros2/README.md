ROS 2 Type Resolver Utilities  
==============================

Process runtime and failure handling
------------------------------------

Managed C++ node entrypoints use `JOSHUA_MAIN(argc, argv)` from
`utils/joshua_runtime.h`. The macro generates a guarded `main()` and delegates
the written body to an ordinary function. It covers argument parsing, clock and
node initialization, and callbacks executed by the node's executor. Direct
`RunNode<T>()` calls use the same guard; nested runners reuse the outer boundary.

```cpp
JOSHUA_MAIN(argc, argv) {
  return ros2_utils::RunNode<PositionPublisher>(argc, argv, "position_publisher");
}
```

`RunNode<T>()` validates the config, calls `JoshuaInit(config)` to install the
process clock, constructs the node, registers its executor, and notifies the
launcher of readiness. A finite executor wait also observes worker failures.
Nodes that request multiple executor threads keep ROS callback-group scheduling
and guard each executor worker. This keeps MCU stop callbacks concurrent with
ordinary work while reporting worker exceptions through the process runtime.
`rclcpp` handles shutdown signals through its handler thread; the shutdown
callback requests cancellation of ordinary serial waits.

On exit the runner cancels ordinary work, releases executor/node resources,
tears down the board cache, releases communication resources and their workers,
and shuts down the ROS context. If ROS already received a shutdown signal its
context may have stopped first. Cleanup catches and reports failures while
continuing to other resources. It cannot make a throwing C++ destructor safe
from outside that destructor; device destructors use the shared `RunCleanup()`
boundary themselves.

Use `joshua::StartWorker(name, callback)` for owned worker threads, and join them
from their owner. A worker exception reports failure and requests process
shutdown; it never escapes the thread entrypoint. Unexpected callback/clock
exceptions propagate to the process guard. Expected device errors continue
through status returns. A cleanup failure preserves a nonzero process result.
Diagnostics do not depend on RobotTime.

Cleanup scopes allow best-effort stop commands after ordinary waits have been
cancelled. Serial commands use steady elapsed deadlines; a hung device can
require the launcher's bounded SIGTERM/SIGKILL escalation. Teardown
is an attempt, not an acknowledgement that a device physically stopped.

Standalone Xbox publishing accepts an optional preset path before ROS options;
without it `JoshuaInit()` uses protobuf clock defaults, including the documented
startup fallback. No Python RobotTime implementation is introduced. See
[`NodeManager`](../launcher/node_manager/README.md) for process ownership and
readiness limitations.

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
host sensor publishers. Use `DEVICE_BUS` for commands and board-backed feedback
on one bus. Mixed JoshuaWire scalar/actuator setups also support `MCU_BUS`.

All ROS actuator runtimes start disabled, including STS3215 and legacy stepper
presets. Driver construction is forced to remain disabled. Enable deliberately
through the configured torque topic or `~/<actuator>/enable` Trigger service.
This changes prior standalone startup behavior; existing command-only producers
must add an explicit operator enable step.

Mixed MCU bus
-------------

[`mcu_bus`](mcu_bus.h) hosts scalar subscriptions and actuator commands in one
process, sharing the production board cache and coordinator. Give all consumers
of the MCU the same `node.id` and `node_type: MCU_BUS`. The reference
[preset](../config/config_preset/example/teensy_mixed_scalar.pbtxt) declares real
hardware and must be reviewed before a deliberate run.

Startup configures outputs but leaves them disabled. A BOOL `/<actuator>/torque`
command explicitly enables/disables them; position topics retain native driver
units. `~/estop` is a Trigger service in a separate callback group with two executor
threads, so a blocked ordinary callback cannot hold the stop behind it. When a
node owns multiple boards, it starts every stop concurrently before waiting for
acknowledgments; a stalled board cannot delay stop dispatch to the others. Stop
acknowledgment is protocol evidence; physical stop verification is unsupported.
After a fault/ESTOP, recovery and a fresh process/session are deliberate operator
steps. No subscription, enable or target is automatically restored.

Scalar topics publish serialized `PerceptionPacket` messages as
`std_msgs/UInt8MultiArray`, retaining the named sensor, native unit code, validity,
boot/clock identity, acquisition sequence/time, receipt age and loss counters.
The packet's top-level timestamp is unset: no device-to-ROS clock mapping exists.
The companion `<topic>/health` String reports usable, invalid/stale, or the source
error. Record both topics; decoding uses `perception_packet.proto`. Receipt
freshness does not establish absolute device acquisition age.

Position, camera, lidar, audio and Xbox publisher callbacks use
`utils/robot_timer.h` with double-second RobotTime deadlines. The executor polls
those deadlines; a paused RobotTime pauses publication. Missed periods are
skipped, and camera/lidar callbacks only read their own configured stream. See
[publisher timers](../docs/ROBOT_TIME.md#publisher-timers) for clock-jump behavior.
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
The actuator node interprets scalar/array positions as native units and JointState
as SI. Subscriptions contain only topic and message type, not actuator encoding.
Normalized scalar producers must convert before publishing.
Both endpoints must use the same wire type; Float32 and Float64 do not match.

JointState command topics use `config.topic()` directly and can have any valid ROS
topic name. The message's `name` array selects `actuator_name`; no synthetic
`/position` topic is constructed. Names must be nonempty and unique. Each numeric
array must be empty or match the names array, all supplied values must be finite,
and at least one of position, velocity, or effort must be present.

The decoder emits `ActionPacket.joint`, retaining optional double-valued
position, velocity, and effort in SI units (rad, rad/s, N*m for revolute joints;
m, m/s, N for prismatic joints). An omitted value is distinct from zero. It also
preserves the selected joint name, `header.frame_id`, and the source timestamp in
`ActionPacket.timestamp` as double seconds. The ROS nanosecond component must
be below 1e9; zero and negative source times are retained within double
precision. Metadata does not schedule execution, enforce
freshness, identify a clock domain, or trigger coordinate transforms. Those
policies belong to future clock/PTP and coordinate-handling work.

`ActionPacket.joint` is the motion payload for every actuator. Optional position,
velocity, and effort distinguish omission from zero. `position_encoding` selects
`POSITION_NATIVE` (default), `POSITION_SI`, `POSITION_NORMALIZED_ZERO_ONE`, or
`POSITION_NORMALIZED_MINUS_ONE_ONE`. The separate `units` field applies only to
velocity and effort: NATIVE preserves existing driver settings; SI means physical
velocity and effort. JointState sets SI position encoding and SI velocity/effort
units explicitly, preserving joint name, frame, timestamp, and supplied fields.

STS3215 and stepper support native position/velocity combinations and SI
position-only commands (radians converted to ticks/degrees). They reject effort;
use presets to enable/disable torque. The legacy TI demo motor driver is retired;
AM243 examples use the stepper driver over JoshuaWire. Drivers validate the entire payload before writes. Channel failures are returned; a
multi-field command is not a transactional hardware operation.

Scalar topics retain native values: `/position` maps to position, `/speed` and
`/velocity` to velocity, `/effort` to effort. Legacy `/torque` on STS3215/stepper
maps to enable/disable presets. `/dc` remains unsupported by runtime motor drivers. The standalone Pybricks tool defines
native effort as duty percent and requires it to be sent alone.

Normalized positions map through configured operational limits once, then become
POSITION_NATIVE before driver execution. Nonfinite/out-of-range input and invalid
limits are rejected, never clamped. Native and SI positions are left for drivers.
Position encoding does not normalize velocity or effort.
Header metadata does not imply scheduling, clock synchronization, or transforms.

Position feedback still publishes one named position per sensor (using
`sensor_name`), converted to radians with the original RobotTime acquisition
timestamp. It does not fabricate velocity or effort. Use an aggregator for
synchronized multi-joint feedback.
Device zero references are retained; URDF offsets and mechanical calibration
are not inferred.

Camera and lidar publishers also preserve packet timestamps in double seconds.
ROS conversion checks range and rounds the fractional seconds to nanoseconds; see
[robot time](../docs/ROBOT_TIME.md#acquisition-timestamps-and-ros-conversion).

Hardware and downstream constraints
----------------------------------

The shared C++ runner (`RunNode<T>` in `node_runner.h`) loads and validates the
full config before constructing a node, including when launched directly without
`NodeManager`. Position and actuator constructors rely on that shared check.
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
| Inference still produces FLOAT32; trajectory supports FLOAT32 and JOINT_STATE | JointState waypoints require SI values and preserve optional fields. Driver capabilities still apply; changing the wire type does not add velocity/effort control. |
| Dataset message types/shapes can change | Update dataset/model expectations and keep topic types stable during recording. |
| Multiple command sources can target a device | Coordinate command ownership externally; message-type support does not add arbitration. |

Tests cover typed pub/sub, conversion failures, named-joint selection and units,
and shared runner validation. No hardware is required.

## C++ node logging

C++ nodes and utilities use `JOSHUA_LOG(INFO) << message` (also DEBUG, WARNING,
ERROR and FATAL). The wrapper preserves the call site's file, line and function,
and uses the actual ROS logger name, including node remapping. Configure via:

```protobuf
general { ros2_log_mode: ROS2_LOG_BOTH }
```

BOTH is the protobuf default: glog writes to stderr and ROS writes to `/rosout`
and its configured file logger. ROS console output is disabled process-wide in
this mode, including ROS library console diagnostics, to avoid duplicate output.
`ROS2_LOG_ROS` uses normal ROS destinations; `ROS2_LOG_GLOG` uses glog for Joshua
messages. ROS output flags such as disabling rosout remain honored.

ROS severity settings filter the ROS destination; glog minloglevel filters glog,
and DEBUG additionally requires glog verbosity >= 1 for that destination. FATAL
always terminates the process after logging, regardless of backend. Startup and
post-shutdown messages fall back to glog because ROS routing is unavailable.

The mode covers Joshua C++ node logs, not Python loggers or direct driver/library
LOG calls. The latter remain glog. A returned status carries no source location;
returned errors are logged at the node boundary with topic context. Drivers preserve
the failure details in their status rather than also logging the same error.
Destructor cleanup failures are logged locally when no caller can receive a status.
Standalone C++ utilities have no config input and use BOTH. The logging context
is process-wide, matching Joshua's one-node-per-process launcher; component
composition would require per-node contexts.

## Audio publishing

`AUDIO_PUBLISHER` owns ALSA microphones configured through `alsa_audio_config`.
See `config/config_preset/example/publish_audio.pbtxt`. This preset opens real
hardware; select the microphone explicitly (for example
`plughw:CARD=Webcam,DEV=0`) before launching it. Docker needs `/dev/snd` access.
The images install `libasound2-dev`; deployed packages need the ALSA runtime.

Each `std_msgs/msg/UInt8MultiArray.data` contains one serialized
`robot.perception.PerceptionPacket` with the `audio` oneof. Layout is empty.
Decode with the generated perception_packet proto. Audio is interleaved `s16le`,
and its length is `frames * channels * 2`. Sample rate, channels, frame count,
source name, sequence, and discontinuity are carried with every chunk.
`timestamp` estimates the first sample's configured RobotTime in seconds from unread
ALSA frames. It is not a hardware timestamp or a synchronization guarantee.

The source sample rate and `frames_per_chunk` determine publication cadence;
leave `publish_rate_hz` unset (zero). One source read fans out to all configured
topics. Reads are nonblocking and incomplete chunks accumulate. On capture
failure the partial chunk is discarded and the device is reopened. The first
chunk after startup/recovery sets `discontinuity`; sequence IDs increase across
recovery but reset on process restart. Consumers must reset windows on either
signal, sequence gaps, or stale input. A stalled source logs an error, never
fabricates silence. Keep subscription QoS compatible with the preset.

`capture_buffer_duration_ms` requests an ALSA buffer of 1..1000 ms; omitting it
preserves the 200 ms default. Polling uses the **negotiated** buffer and period,
since hardware may choose a different capacity. The timer aims for half a chunk
duration, batching tiny chunks at a preferred minimum of 1 ms, but never waits
longer than one negotiated period or half the buffer duration. The buffer limit
overrides that preferred minimum. This reserves headroom for delayed callbacks;
it is not a real-time scheduling guarantee.

Each callback can drain a buffer's worth of packets plus one partial packet,
then yields even if a plugin continuously supplies data. Short reads continue
until the current packet completes or ALSA has no more data. Large chunks thus
accumulate across polls, while small chunks have a budget proportional to their
required throughput. After reopening, the publisher refreshes the timer from the
newly negotiated buffer. Device failures use a one-second retry backoff; pending
chunks keep normal polling. Sustained outages are reported after two seconds,
with subsequent warnings at most once per second.

Use `//robot/perception/audio:alsa_audio_test` for a hardware-free ALSA null
plugin check. Config validation checks format bounds and duplicate device strings;
ALSA aliases for the same physical device cannot be detected statically.
`//robot/perception/audio:audio_capture_test` uses a finite fake PCM buffer and
virtual time to cover large and tiny chunks, short reads, delayed polls, changed
buffer sizes after recovery, and bounded work for always-ready sources.

## Shared board runtime

`DEVICE_BUS` composes actuator commands and board-backed position/scalar
observations in one process. Assign every consumer of a bus the same node ID
and `node_type: DEVICE_BUS`. Existing factories share the board and transport;
no transport implementation is selected by the node. Position publishers retain
the existing numeric/JointState contracts. `MCU_BUS` remains supported for existing
JoshuaWire scalar/stepper configurations.

The composed owner explicitly disables actuators at startup and tears them down
before releasing boards. Steppers require `require_explicit_enable: true` so their
driver initialization cannot enable early. Camera/lidar acquisition remains in
its existing standalone nodes. This feature does not qualify physical motion,
link-loss stopping or a particular rig.

## Shared control lifecycle

Each actuator has one transport-independent `ControlGate` shared by its topics.
`Subscription.source_id` (default `default`) identifies a configured producer.
`Actuator.control_policy.initial_source` selects the initial producer; other
producers cannot enable or move the actuator. This is topic-based arbitration,
not publisher authentication. Use separate JointState command topics for multiple
sources; the existing numeric topics keep their actuator-name contract.

`command_timeout_ms` defaults to 1000 ms. Accepted motion renews the steady-clock
lease; repeated enables, telemetry, rejected inputs and board heartbeats do not.
Expiry attempts a disable and latches a stop, checked every 10 ms and at command
admission. Host scheduling/driver delay applies. Link loss still requires the
selected device's watchdog and physical qualification.

An optional positive `max_input_age_ms` requires motion as JointState with
nonzero, increasing stamps in the receiver's ROS clock. Future, old, repeated and
pre-selection/pre-recovery stamps are rejected. Numeric motion has no acquisition
time and cannot be used with that bound; absent the bound, only local command
inactivity is enforced. Producers must independently gate their input freshness.

`~/<actuator>/stop`, `/recover`, `/enable` and `/select_<source_id>` are Trigger
services. Selection requires disabled state. Stop/driver failure/lease expiry
requires explicit recovery, then enable; recovery never replays motion. These
services clear only host state and cannot clear a firmware latch. JoshuaWire
fault/ESTOP still needs deliberate device recovery and a fresh runtime. Composed
`~/estop` also interlocks all host gates and invokes available board stop support.
No host acknowledgment is physical stop evidence.

## Observation readiness

Position, camera, lidar and board scalar publishers share `ObservationSource`:
one acquisition worker and one bounded latest sample per configured sensor,
with all of its output topics reading that sample. Reads never run on the ROS
executor. Readiness requires validated data; invalid samples, acquisition errors
and stalled sources remain observable. Repeated scalar acquisitions do not
refresh the last-valid time.

Every output topic has `/status` (`UInt8MultiArray`, serialized `PerceptionPacket`
with `observation_status`) and a short `/health` String. Status includes starting,
ready, invalid, stale, error or stopped state, optional last-valid receipt age,
and valid/invalid/error counters. Scalar `/health` retains its existing summary.
The status timer runs every 50 ms independently of acquisition. Configure
`sensor.observation_policy.stale_after_ms` (positive, default 1000 ms). Native
payload publication pauses when unusable; invalid/stale scalar packets remain
inspectable with `usable=false`. No stale sample becomes a new measurement.

Sensor construction errors are reported through status until deliberate restart;
no board session or subscription is automatically restored. Acquisition workers
are joined on shutdown after requesting backend teardown. Driver calls must be
bounded or support teardown cancellation; a permanently blocked backend can still
hold shutdown. This does not impose a physical acquisition/stop-time guarantee.
Audio retains its separate streaming contract.
