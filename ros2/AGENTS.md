# ROS 2

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../docs/CODING_GUIDELINES.md). Read the local
[README.md](README.md) before editing. This file owns the rules and architecture
contract for `ros2/`; child directories inherit it unless they add local rules.

## Architecture contract

Component nodes own their ROS configuration, callbacks, publication state, and
component lifecycle. They use the existing factories for hardware and the shared
node runner for process setup, executors, error boundaries, and cleanup.
Factories, board/transport owners, and drivers retain their existing contracts;
ROS adapters do not open a second connection to a shared bus. Component nodes
may host multiple configured devices and outputs.

Dependencies flow from nodes to configuration, robot interfaces/factories, and
ROS utilities. Hardware implementations remain under `robot/`. Reuse topics,
services, actions, QoS, diagnostics, logging, and rosbag2 before adding parallel
infrastructure. Keep sensor-specific behavior in its component node or driver.

## Component source layout and shared utilities

- Keep each managed component node's complete class and implementation in one
  canonical `.cc`, with a direct include of `ros2/node_runner.h` and a
  `JOSHUA_MAIN` entrypoint calling `ros2_utils::RunNode<Component>(...)`.
  Build that source directly with `ros2_cpp_binary`.
- Do not create node headers, forwarding main files, node inheritance for reuse,
  or a generic node that hides a component's behavior. Existing split nodes
  migrate when their implementation is changed.
- Focused shared utilities may have headers and libraries when multiple callers
  need a stable contract and no existing utility owns it. State the concrete
  callers and responsibility in the PR. This exception does not allow moving a
  node class to a header or adding another executor, driver owner, or runtime.
  [ActuatorCommands](utils/actuator_commands.h) shares command handling between
  the standalone and mixed nodes on their caller-owned ROS node.

## Initialization, acquisition, and shutdown

A change that moves acquisition to a worker must meet the
[perception backend contract](../robot/perception/AGENTS.md#perception-backend-contract)
before starting that worker. The component owns acquisition scheduling and
sample state where the driver does not already provide a cache. Reuse the
coordinator cache for MCU scalars; preserve audio's chunk flow. Each physical
source has one acquisition owner shared by its configured output topics.

Name one owner for each driver's initialization, recovery, and teardown. Report
whether an initialization error aborts startup or leaves a registered unhealthy
source for bounded retry; camera recovery must reapply the configured settings.
A factory must not silently initialize a driver that the node initializes again.

Choose and test the scheduling contract explicitly. The baseline is the target
branch's behavior at the recorded commit, not an earlier draft PR. Current camera
and position reads run in each output's RobotTime callback; changing to one
steady-clock acquisition at the maximum output rate is a behavior change.
For example, 10 Hz and 30 Hz outputs can produce 40 read attempts versus 30
shared acquisitions per second. State sample sharing, skipped periods,
RobotTime pause/jump behavior, and timestamp/freshness clock domains separately.
Publication must retain the [RobotTime contract](../docs/ROBOT_TIME.md).

Use `joshua::StartWorker` and ensure completed member records or rollback guards
own cleanup if a later device, timer, or worker fails to construct. The destructor
of an incompletely constructed node does not run. Reserve worker storage before
starting threads and install join guards before any later throwing operation.
Normal shutdown stops admission, requests supported read cancellation, joins
workers, then releases drivers and shared resources in owner order. Never detach
a worker that can still access node memory.

## Hardware-free real-node tests

Compile the actual `.cc` in a narrowly visible, `testonly` target with only the
entrypoint excluded. Tests include that implementation, inject a test source or
use factory-backed mock boards/PTYs, and execute real callbacks in a ROS executor.
Do not create a production node library or duplicate the implementation for tests.
See [camera_publisher_test.cc](camera_publisher_test.cc),
[actuator_subscriber_test.cc](actuator_subscriber_test.cc), and the test-source
targets in [BUILD](BUILD).

Keep [mcu_bus_test.cc](mcu_bus_test.cc)'s actual stop-service tests and the
[PTY mixed-binding test](../robot/board/joshua_wire/mixed_binding_test.cc).
New acquisition code needs a controlled blocked read while health/stop callbacks
remain responsive, cancellation and join within a declared deadline, stale/error
reporting, clock pause/jump and multi-rate cases, and rollback when a later source
or worker creation fails. Test the cancellation contract itself; moving I/O off
the executor is not evidence of bounded teardown. Unsupported cancellation needs
a separate supervised-process escalation test, with its limitation stated.

Run changed targets and the canonical test services from the root instructions.
No real camera, serial port, NIC, or physical actuator is needed for these tests.
