# Production code cleanup for perception acquisition

**Status: documentation only; perception cleanup remains proposed.**

Keep sensor acquisition and publication understandable within Joshua's existing
factory, driver, board, and ROS node boundaries. The recommended direction is for
ROS nodes to own acquisition scheduling and sample state, while reusing caches
already owned by drivers or board coordinators. Add workers where reads block.

This proposal records the alternatives discussed while reviewing
[PR #145](https://github.com/Joshua-AI-Robotics/Joshua/pull/145), at commit
`59e47085`. Its C++ observation runtime is separate from the Python MCP reader
contract in [MCP_OBSERVATION_DESIGN.md](MCP_OBSERVATION_DESIGN.md). The single-source
node cleanup in [PR #159](https://github.com/Joshua-AI-Robotics/Joshua/pull/159) is
related work. These PRs are review context, not implementation prerequisites for
merging this document.

## AI-assisted code generation rules

The repository-wide operating rules are in
[AGENTS.md](../AGENTS.md#ai-assisted-code-generation). Directory-specific rules
start with [ros2/AGENTS.md](../ros2/AGENTS.md), discovered alongside its existing
README and inherited root guidance. These instruction files define how agents
review and extend existing code; this proposal records the ownership options
and implementation plan below.

## Problem and behavior to retain

PR #145 adds `ObservationPublisher`, `ObservationSource`, `ObservationState`, and
a generic `ObservationNode`. The adapter creates a worker per sensor; that worker
reads a driver and updates a latest-sample cache. ROS timers publish data,
metadata, and health from that cache. See its
[adapter](https://github.com/Joshua-AI-Robotics/Joshua/blob/59e470858c8135b88ae6c742a8c182f49710c40b/ros2/observation_publisher.h)
and [worker/state implementation](https://github.com/Joshua-AI-Robotics/Joshua/blob/59e470858c8135b88ae6c742a8c182f49710c40b/robot/perception/interfaces/observation_source.h).

The useful behaviors are:

- One acquisition owner per configured sensor, shared by all its output topics.
- Blocking sensor reads do not occupy ordinary ROS publication/control callbacks.
- Bounded storage, validated data, observable freshness and errors, and preserved
  sample identity, timestamps, units, and frames.
- Explicit worker cancellation, joining, and driver cleanup.

The concern is the extra chain of runtime owners between a node and its driver.
MCU scalar sensors also already expose cached samples, making another acquisition
worker unnecessary for that path. Scheduling, synchronization, and sample state
still need an owner; moving a header into a different directory does not remove
those responsibilities.

## Placement options

| Option | Implementation | Benefit | Cost or limitation |
| --- | --- | --- | --- |
| **1. ROS node owns acquisition — recommended** | Each node owns per-sensor state, output timers, and optional acquisition workers in its `.cc` implementation. | Clear lifetime; retains the existing driver API and single-source node structure. | Some repeated lifecycle code across nodes; share small helpers where the behavior is actually common. |
| **2. Driver owns acquisition** | A streaming driver owns its acquisition worker and exposes a bounded, nonblocking latest-sample operation. | Useful to consumers outside ROS; device-specific acquisition stays near the device. | Larger driver/API change. Blocking reads and cached reads need explicit contracts, including sample identity and unavailable data. |
| **3. Factory constructs a buffered driver wrapper** | The factory returns a `PerceptionInterface` wrapper that owns a driver, worker, and cache. | Callers keep a familiar interface. | Retains the extra runtime abstraction behind the factory; also changes the meaning of reads unless the API makes buffering explicit. |
| **4. ROS callback groups and executors** | Acquisition callbacks use separate groups, potentially on a separate executor; the node owns the cache. | Uses ROS execution primitives instead of a custom worker wrapper. | Blocking reads still consume executor threads. Capacity, callback groups, cancellation, and shutdown require careful coordination. |

ROS supports assigning callback groups to different executors. A separate group
alone does not reserve a thread, and callbacks in the same mutually exclusive
group remain serialized even with a multithreaded executor. See the official
[callback-group guide](https://github.com/ros2/ros2_documentation/blob/humble/source/How-To-Guides/Using-callback-groups.rst)
and [executor documentation](https://github.com/ros2/ros2_documentation/blob/jazzy/source/Concepts/Intermediate/About-Executors.rst).

## Recommended ownership

Use option 1, combined with existing driver/coordinator caches where available.
The complete C++ node class and its small `JOSHUA_MAIN` / `RunNode<T>()` entrypoint
stay in one `.cc` file per node. Shared packet operations can remain utilities;
they do not need to own nodes, threads, or devices.

| Existing component | Responsibility |
| --- | --- |
| [PerceptionFactory](../robot/perception/factory/perception_factory.h) | Resolve configuration and board dependencies, construct drivers, and report initialization failures through an explicit initialization path. No permanent factory-owned thread pool or hidden global sample cache. |
| [PerceptionInterface](../robot/perception/interfaces/perception_interface.h) and concrete drivers | Device I/O, device-specific validation and timing facts, and a documented read/cancellation contract. Cached reads preserve sample identity and original receipt times. |
| [BoardFactory](../robot/board/factory/board_factory.h) and board/coordinator | Existing bus ownership, shared sessions, transport scheduling, and any already-acquired telemetry cache. |
| ROS publisher and composed bus nodes | Per-sensor state, acquisition scheduling where needed, output timers, readiness/health publication, and worker lifetime. |
| [Packet utilities](../ros2/utils/packet_parser.h) | Shared validation and conversion functions. Common metadata/status updates can be small functions operating on caller-owned state. |
| [Process runtime](../utils/joshua_runtime.h) and [node runner](../ros2/node_runner.h) | Guarded startup, cancellation, unexpected worker failure reporting, and process cleanup. Node-owned workers use `joshua::StartWorker()` and are joined by their owner. |

The existing [audio publisher](../ros2/audio_publisher.cc) provides a useful
ownership example: it owns per-source state and distributes captured data to
configured publishers. Its bounded audio reads have a different scheduling
contract from blocking camera or lidar reads.

## Acquisition by sensor type

| Sensor path | Proposed acquisition handling |
| --- | --- |
| Camera | The camera node owns a worker for blocking capture and a latest validated frame shared by its output topics. Preserve explicit initialization and recovery of capture settings. |
| Lidar | The lidar node owns a worker for blocking scan assembly and a latest validated scan. Preserve motor stop and read cancellation during shutdown. |
| Board-backed position feedback | The owning ROS node uses a worker when feedback acquisition performs blocking bus I/O. Reuse board-owned acquisition if that capability already exists. |
| MCU scalar stream | Read the existing coordinator cache through [BoardScalarSensor](../robot/perception/scalar/board_scalar_sensor.h) and [ScalarSensor::Latest()](../robot/board/interfaces/scalar_sensor.h). Add publication and health handling without another polling worker. |
| Audio | Retain its existing streaming/chunk flow. A latest-only cache would discard required chunks and is not a general replacement for audio buffering. |

If future drivers make the choice ambiguous, expose a documented capability or
separate read API. A factory must not silently change a synchronous acquisition
call into a cached read. A driver that already owns acquisition must not acquire
a second worker merely because it has several ROS output topics.

## Node-local acquisition and publication

For a blocking driver, keep a per-sensor record inside the node implementation.
It owns the driver, latest immutable packet, health and identity state, a mutex,
and its worker. Several configured output topics reference that one record.

The worker reads the driver without holding the sample-state mutex, validates the
result, and briefly locks the record to update its packet and health. Publication
callbacks take a snapshot and convert/publish it. A separate health callback can
report stale data while acquisition is waiting. Repeated reads of an MCU cache
must not renew a sample's receipt time or make it a new acquisition.

Preserve the configured RobotTime timestamp and publication contracts. Use
monotonic elapsed time for host receipt-age calculations. Specify the acquisition
scheduling clock explicitly; it must not change accidentally while moving code.
Keep process readiness distinct from readiness established by a valid sensor
sample. Expected device errors remain status results; unexpected worker failures
follow the existing process runtime.

During teardown, stop scheduling ordinary reads, request backend cancellation or
teardown as appropriate, wake and join owned workers, then release their driver
handles. Preserve the established bus owner and process cleanup order. Moving
I/O off a ROS executor does not eliminate contention on a shared physical bus;
transport deadlines and stop priority remain board/coordinator responsibilities.

## Proposed implementation sequence

1. Preserve complete node implementations in the camera, lidar, position, and
   composed bus `.cc` files, using the shared node runner.
2. Move per-sensor acquisition and publication ownership into those nodes. Remove
   mandatory `ObservationSource` wrapping and generic `ObservationNode` ownership
   from the PR #145 design; keep sample/status data contracts that are needed.
3. Reuse the MCU scalar cache directly. Add node-owned workers only to paths
   whose reads block, and avoid duplicate acquisition across output topics.
4. Share small validation, conversion, and metadata-update functions through
   existing utilities. Keep state owned by the node or its existing driver.
5. Validate behavior before removing the replaced implementation. Keep changes
   to enable policy, command leases, stop/recovery, and normal actuator teardown
   separate from this structural cleanup.

## Verification for implementation PRs

Use hardware-free driver doubles and existing board/PTY fixtures to verify:

- A blocked read leaves ROS health and control callbacks schedulable; shared-bus
  stop dispatch is checked separately.
- Multiple output topics share one acquisition and retain independent configured
  publication rates, sample identity, and original timestamps.
- Cached scalar reads do not add workers or refresh old sample validity/age.
- Invalid data, errors, staleness, and recovery remain observable; camera
  initialization/recovery, audio chunk continuity, and lidar cleanup are retained.
- Shutdown cancels supported reads, joins workers, and preserves cleanup/failure
  reporting. Worker exceptions reach the process runtime.
- Multi-actuator creation, source selection, enable/stop/recovery policy, leases,
  and normal driver teardown retain the implementation baseline's contracts.

Run the targeted tests and the supported Compose test services described in
[CONTRIBUTING.md](../CONTRIBUTING.md#testing-and-ci) for implementation changes.
Physical runs remain governed by [AGENTS.md](../AGENTS.md#hardware-safety--read-this-first).
