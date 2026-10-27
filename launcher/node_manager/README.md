# Node Manager

`NodeManager` is the launcher's single owner for configured ROS node processes
and simulation. It lives under `launcher/node_manager/`, alongside the launcher
entrypoints that use it, in `node_manager.h` and `node_manager.cc`.

## Lifecycle

The launcher constructs one manager, calls `Initialize()` to load and validate
its preset, and initializes RobotTime from `manager.config()`. `LaunchAllNodes()`
resolves every executable before starting the configured graph. `MonitorNodes()`
observes child exits and requests shutdown on an unexpected required-node exit.

For simulation, the existing simulation entrypoint receives that same manager.
It launches accompanying ROS nodes when configured, calls `LaunchSimulation()`,
and uses `MonitorNodes("simulation")` so normal simulation completion also stops
its accompanying graph. There is no second process owner in simulation mode.

```cpp
launcher::NodeManager manager(config_path);
joshua::ThrowIfError(manager.Initialize());
JoshuaInit(manager.config());
joshua::ThrowIfError(manager.LaunchAllNodes());
joshua::ThrowIfError(manager.MonitorNodes());
```

The manager owns signal installation/restoration, executable and runfiles
resolution, spawning, readiness, polling, signaling, reaping, and exit history.
Private helper methods implement these steps in the same file. Each private
`NodeRecord` contains plain launch/status data and its control descriptors;
records do not have independent ownership behavior or wrapper classes.
`GetNodeStatus()` returns a copy of current state and retained exit records,
including node IDs/types and topic metadata. The API is used on the launcher
thread; one manager may own a launcher process at a time.

Configured nodes and simulation use the launcher's runfiles tree, which Bazel
refreshes when building the launcher. A child's standalone `.runfiles` tree may
be stale and is only used when launcher runfiles are unavailable.

`StartProcess(LaunchSpec)` also registers explicitly supplied jobs, including
the hardware-free test child. `Initialize()` and graph configuration are only
available before any job is registered. Processes are never automatically
restarted, and a trajectory process may complete successfully without failing
the remaining graph.

## Process ownership

Each job is `launcher → guardian → application → descendants`. The existing
guardian behavior is retained: it reports the actual application PID and wait
status, stays alive after application exit to reserve the process group, and
kills its group if its launcher dies. The application PID and guardian/group
PID are distinct fields in `NodeStatus`.

The manager enables Linux subreaping and reaps only owned process groups;
unrelated child processes are not consumed through `waitpid(-1)`. Descendants
must remain in their assigned process group. Processes that detach with
`setsid()` or change groups are outside this ownership contract. Abrupt launcher
death forces termination and cannot run application teardown.

All node records remain available after reaping. They retain startup state,
readiness, application exit code/signal, expected exit, forced shutdown,
errors, and reaping status. Only the manager signals or reaps its jobs.

## Readiness and shutdown

Managed C++ nodes send one byte through `JOSHUA_READY_FD` after configuration,
RobotTime setup, node construction, and executor registration. The shared Python
node runner sends the same notification after construction; it does not implement
RobotTime. The startup channel is closed after notification. Readiness means the
node was constructed, not that a lazy sensor produced its first valid sample.

Inference and simulation have not adopted this protocol; their records report
executable startup separately and never claim application readiness. The graph
has a 30-second readiness budget for nodes using the protocol. Exec and
readiness failures roll back partial graph startup.

Signal handlers only set a flag. Normal control flow performs shutdown and
returns a status to the guarded launcher, without deep `exit()` calls. The
default policy gives SIGINT three seconds and SIGTERM two seconds, followed by
SIGKILL and a one-second reaping window. `ShutdownPolicy` allows callers to
supply explicit grace periods. `Shutdown()` is idempotent and closes the job's
control channels after reaping while retaining its status record.

OS process grace periods use host elapsed time so shutdown remains available
when RobotTime fails or pauses. Driver transaction deadlines still use RobotTime.
OS tasks that cannot be reaped within the kill window are reported as a shutdown
failure.

## Configuration and tests

`config::ValidateConfig` remains a separate pure validation library; it does
not construct drivers, open devices, or launch nodes. `Initialize()` and the
C++ ROS runner call it before creating node-owned hardware resources. Factories
retain their own defensive construction checks.

`node_manager_test.cc` exercises the manager. `node_manager_test_child.cc` is a
**test-only executable** that simulates readiness, failures, finite completion,
stalled processes, and descendants; it is not launched by normal robot presets.
The Bazel targets are `//launcher/node_manager:node_manager` and
`//launcher/node_manager:node_manager_test`. Run tests only when validation is requested.
