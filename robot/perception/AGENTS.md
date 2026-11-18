# Perception

Follow [robot/AGENTS.md](../AGENTS.md), the repository instructions, and shared
coding guidelines. Read [README.md](README.md) before editing. This file owns
the local architecture contract and rules for `robot/perception/`.

## Architecture contract

Drivers own sensor I/O, device-specific validation, and acquisition facts behind
PerceptionInterface. PerceptionFactory selects implementations and resolves board
or comm capabilities. Preserve independent drivers rather than adding a generic
acquisition runtime around every sensor. ROS nodes own publication and scheduling
when the driver/coordinator does not already acquire/cache samples.

## Initialization ownership

The baseline at develop `6cb9adca` has these owners. Preserve them during a
refactor unless an explicit migration changes both factory and node callers.
[PerceptionFactory](factory/perception_factory.cc) owns selection and assembly;
calling `Init()` again in a new wrapper is not a recovery policy.

| Backend | Initial setup owner | Failure and recovery baseline |
| --- | --- | --- |
| OpenCV camera | CameraPublisher calls `Init()` per physical source | Invalid settings fail startup; unavailable initialization remains registered for one-second RobotTime retries. Read failure closes the source; node-owned reinitialization reapplies settings. |
| ALSA audio | AudioPublisher calls `Init()` | Initial failure aborts construction. Capture code reopens after device failure; the node owns retry cadence and refreshes the negotiated polling schedule. |
| LDS01 lidar | PerceptionFactory calls driver `Init()` before returning | The node logs/skips a source whose factory initialization fails; it has no registered automatic retry for that source. |
| Board position | BoardFactory initializes the board before resolving its channel | Sensor `Init()` is a no-op; board lifetime/recovery stays with the shared owner. The position node skips failed factory resolution. |
| Board scalar stream | Factory resolves the shared board and subscribes the scalar source | Sensor `Init()` is a no-op; MCU node construction fails if source creation fails. Closing a scalar lease must not close another consumer's board. |

A migration must choose abort-versus-registered-unhealthy behavior explicitly,
including retry cadence, settings reapplication, and cleanup if a later source
fails. These existing differences must not disappear behind a generic factory
or worker constructor.

## Perception backend contract

Before adding asynchronous acquisition, document each backend's read behavior,
initialization owner, failure policy, cancellation mechanism, maximum elapsed
wait, and fallback. A cancellation request must be callable without first taking
a mutex held by the blocked read. Cancellation and teardown are separate operations.

`CvCamera::GetData()` holds `cap_mutex_` across OpenCV capture and `Teardown()`
needs that mutex. The current API has no interrupt operation or verified capture
timeout for this backend. Calling teardown from another thread does not unblock
it. Do not claim an in-process shutdown bound or introduce a worker assuming it
does. Establish and test a backend-specific bounded read/interrupt contract, or
keep the uninterruptible backend inside the existing supervised node process.
The launcher can escalate termination under its configured policy; termination
cannot promise destructor execution, reaping an uninterruptible OS task, or a
physical stop.

For serial-backed reads, use the existing transport's total/idle deadlines and
process cancellation; state the effective configured bound, including queue wait.
For board scalar caches, preserve acquisition identity, original receipt times,
validity, boot/clock identity, and loss counters. Reading a cache is not a new
acquisition. Audio requires its bounded chunk/buffering contract rather than a
latest-only frame cache. Keep device acquisition time distinct from receipt age
and ROS publication time.

## Verification

Use fake capture/audio sources, test-only board caches, fake streams, and PTYs.
Test metadata, invalid packets, unavailable devices, initialization/recovery, and
read deadlines/cancellation where supported. A worker migration needs a blocked
read and initialization/worker-start rollback fixture before it claims bounded
cleanup. Run affected `//robot/perception/...` targets in Docker and preserve the
real-node tests required by [ROS instructions](../../ros2/AGENTS.md#hardware-free-real-node-tests).
