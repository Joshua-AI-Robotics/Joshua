# Robot hardware layer

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../docs/CODING_GUIDELINES.md). Read the local
[README.md](README.md) before editing. This file owns the rules and architecture
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
