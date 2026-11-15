# Robot hardware layer

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../AGENTS.md#coding-guidelines). This file owns the rules and architecture
contract for `robot/`; child directories inherit it unless they add local rules.

## Architecture contract

This is a C++ layer independent of ROS node classes, message libraries, executors,
and MCP. Protobuf declarations under `ros2/proto` describe configuration and are
not permission to depend on ROS runtime targets. Test-only ROS integration
fixtures may exercise adapters; production drivers remain ROS independent.

| Directory | Owns | Allowed hardware dependencies |
| --- | --- | --- |
| [action/](action/AGENTS.md) | Motor semantics, command validation, units, calibration, and limits | Board interfaces/channels resolved through factories |
| [perception/](perception/AGENTS.md) | Sensor I/O, device validation, and acquisition facts | Board capabilities or comm capabilities required by the sensor |
| [board/](board/AGENTS.md) | Board protocol, channel mapping, shared sessions, and coordinator caches | Comm interfaces/factory and shared protocol codecs |
| [comm/](comm/AGENTS.md) | Byte movement, NIC/serial ownership, queues, and transport deadlines | Transport backends and system/vendor libraries |

Detailed layer rules live in the linked contracts. Consumers use interfaces and
capabilities; implementation selection stays in the responsible factory.

Use the existing Action, Perception, Board, and Comm factories for configuration
selection and shared resource resolution. Extend sibling implementations behind
those interfaces. Preserve one shared board/transport owner and one reader per
connection. Factories assemble components; they do not become acquisition
schedulers or another process/lifecycle framework. Static configuration policy
is owned by [config/AGENTS.md](../config/AGENTS.md); defensive runtime checks stay
at their operating boundary.

## Hardware topology and latency budgets

For high-bandwidth sensing or tight actuator deadlines, prefer separate actuator
and sensor-acquisition boards with independent physical links. Shared boards/links
remain supported when measurements show sufficient bandwidth and latency headroom
under the intended worst-case workload. Keep feedback needed by the local motor
control loop, such as encoder/current measurements, on the actuator controller.

Queues and priorities manage contention but do not guarantee deadlines. Separate
ROS nodes do not isolate a shared link, and separate boards may still contend for
USB-controller or host resources. Qualify either arrangement against its command,
stop and sensor-loss budgets; see the
[topology comparison checklist](../docs/SERIAL_COMM_QUALIFICATION.md#hardware-topology-comparison-todo)
for measurement requirements and outstanding work.

## Verification

Use test-only mock boards, fake transports, and PTYs, exercising real factories
and drivers. Test invalid commands before writes, transport failure/cancellation,
shared ownership, initialization rollback, and final cleanup where affected.
Run the existing `//robot/comm/testing:transport_boundary_test` when changing public
comm headers, plus affected `//robot/...` targets in the supported Docker stacks.
Physical qualification and firmware flashing follow root authorization rules.

## Robot

Hardware-facing code: everything between a `.pbtxt` config and a physical
motor, sensor, or bus. ROS 2 nodes live in [ros2/](../ros2/AGENTS.md) and
consume these interfaces; nothing here depends on ROS 2 message types.

When choosing boards and physical links, follow
[Hardware topology and latency budgets](AGENTS.md#hardware-topology-and-latency-budgets)
for guidance on shared versus separate actuator and sensor-acquisition hardware.

> This is the code that moves physical motors and opens real buses. Before
> running anything from here, see the hardware-safety section of
> [AGENTS.md](../AGENTS.md).

## Layers

The layer ownership and dependency contract is defined in [AGENTS.md](AGENTS.md#architecture-contract).

`board/` is **mid-migration — read this before touching the actuator path.**
Each layer talks to the one below through an interface, never a concrete type:
a motor driver holds a `BoardChannel`, not a `Serial`, so a motor, a controller
board, and a transport can be chosen independently in config rather than in
code.

The actuator path is there: every `MotorType` `ActionFactory` supports resolves
`board_name` → `BoardFactory` → `OpenChannel` → driver. Position sensors use the same board/channel resolver and shared board cache.
Cameras use OpenCV directly; LDS01 lidar acquires a byte stream from CommFactory.
See [implemented components](../docs/SUPPORTED_COMPONENTS.md) for software paths
and [supported hardware](../docs/SUPPORTED_HARDWARE.md) for curated models,
driver references, example presets, and intended support scope. Separate
[test records](../docs/TESTED_HARDWARE.md) retain measurements and environments
linked to those models by stable hardware ID.

JoshuaWire scalar sensors resolve a board and function ID through the same board
cache as actuators. A FRAME_STREAM board shares one coordinator/session and a
reserved status heartbeat; it never refreshes actuator targets. The mixed runtime
requires `stepper_config.require_explicit_enable`. Scalar packets preserve device
acquisition and local receipt clocks separately, validity, units, boot identity
and loss counters. Device timestamps are not converted to ROS time.
All perceptions select a concrete driver through `SinglePerception.sensor_config`;
the legacy camera/encoder/lidar config wrappers have been removed.

AM243 JoshuaWire serial and EtherCAT configurations now use the shared
`board/joshua_wire/` engine. CommFactory supplies either message-only or paired
message/cyclic capabilities and owns the NIC lifecycle. The legacy TI-demo
board/driver path is retired; `board/am243/` retains AM243 factory regression
coverage, not a separate board implementation. See [JW EtherCAT configuration](../config/AGENTS.md#joshuawire-over-ethercat)
for protocol selection and hardware-validation limits.

## Non-Goals

- ROS 2 node lifecycle, topics, or message types — see [ros2/](../ros2/AGENTS.md).
- Robot-specific values. Joint names, limits, ports, and calibration come from
  the protobuf config ([config/](../config/AGENTS.md)), never from constants
  in a driver.
- Firmware. Board firmware and flashing live in
  [firmware/](../firmware/AGENTS.md); runtime code never flashes a board.

## Before you change this

Follow the directory [architecture contract](AGENTS.md) when adding an implementation.
Communication mechanism and capability boundaries are described in [comm/AGENTS.md](comm/AGENTS.md).
EtherCAT specifics are in [comm/ethercat/AGENTS.md](comm/ethercat/AGENTS.md).

## Joint commands

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
AM243 examples use the stepper driver over JoshuaWire. Drivers validate the entire payload before writes. Shared interface validation checks only
name, field presence, enum validity and finiteness; each driver's implementation
owns supported combinations, SI conversions, float range and operational limits.
Channel failures are returned; a
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

The observation state in `perception/interfaces/observation_source.h` validates
samples and maintains latest-only data plus identity/readiness/freshness/error
state. It owns no driver or acquisition thread. Sensor-specific ROS nodes own
acquisition workers and publication; board scalar streams reuse their coordinator
cache directly. `PerceptionFactory::CreatePerception` selects and constructs a
driver; its caller must now call `Init()` explicitly, including for LDS01 (formerly
initialized inside the factory). The redundant `CreateInitializedPerception` API
and camera recovery wrapper have been removed; their in-repository callers have
migrated. Camera nodes schedule retries, while the camera driver reapplies device
settings on every `Init()`. `RequestStop()` is separate from `Teardown()`; see the
[ROS shutdown contract](../ros2/README.md#observation-readiness) for bounded serial
reads and the OpenCV process-escalation fallback. None of this sends actuator
commands. Shared actuator gates in `action/interfaces/control_gate.h` implement
explicit enable, source selection, local command leases, and latched recovery
above motor drivers and transports.
