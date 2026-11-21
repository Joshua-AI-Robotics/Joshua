# Perception

Follow [robot/AGENTS.md](../AGENTS.md), the repository instructions, and shared
coding guidelines. This file owns
the local architecture contract and rules for `robot/perception/`.

## Architecture contract

Drivers own sensor I/O, device-specific validation, and acquisition facts behind
PerceptionInterface. PerceptionFactory selects implementations and resolves board
or comm capabilities. Preserve independent drivers rather than adding a generic
acquisition runtime around every sensor. ROS nodes own publication and scheduling
when the driver/coordinator does not already acquire/cache samples.

## Initialization ownership

[PerceptionFactory](factory/perception_factory.cc) selects and assembles drivers.

| Backend | Initial setup owner | Failure and recovery |
| --- | --- | --- |
| OpenCV camera | CameraPublisher acquisition worker per physical source | Invalid configuration fails validation. Recoverable device errors close the source and trigger a retry after one RobotTime second. Each open reapplies the capture settings. |
| ALSA audio | AudioPublisher calls `Init()` | Initial failure aborts construction. Capture code reopens after device failure. The node refreshes its polling schedule. |
| LDS01 lidar | LidarPublisher acquisition worker calls `Init()` once | A failed factory or initialization result remains visible on the source status topic until restart. |
| Board position | BoardFactory initializes the shared board. The acquisition worker initializes the sensor. | Failed source resolution remains visible on the status topic. The board owner controls board lifetime and recovery. |
| Board scalar stream | The factory subscribes the stream. BoardNode initializes the sensor and reads the coordinator cache. | Source errors are reported through status. Closing a scalar lease preserves other consumers. Failure to obtain a required board aborts construction. |

## Perception backend contract

Before adding asynchronous acquisition, document each backend's read behavior,
initialization owner, failure policy, cancellation mechanism, maximum elapsed
wait, and fallback. A cancellation request must be callable without first taking
a mutex held by the blocked read. Cancellation and teardown are separate operations.

`CvCamera::GetData()` holds `cap_mutex_` across OpenCV capture and `Teardown()`
needs that mutex. `RequestStop()` prevents further captures. OpenCV provides no verified
interrupt or timeout for a capture already in progress. Calling teardown from another thread does not unblock
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

## Perception

Sensor drivers expose packets through PerceptionInterface and are selected by
PerceptionFactory. Cameras, audio, lidar, position feedback, and board scalar
sensors retain their own I/O and packet semantics. The [contract](AGENTS.md)
owns initialization, read/cancellation, metadata, and verification requirements.
ROS scheduling and publication belong in [ros2/](../../ros2/AGENTS.md).

Production drivers can open real sensors and buses. Follow the root
[hardware authorization rules](../../AGENTS.md#hardware-safety--read-this-first).
