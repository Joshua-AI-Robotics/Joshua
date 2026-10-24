# Node Generator

The `Node Generator` is a core component of Joshua platform, responsible for dynamically instantiating and managing ROS2 nodes based on a configuration file. It acts as an orchestrator, parsing the user-defined robot configuration, launching the nodes, and monitoring their lifecycle.

## Workflow

The Node Generator follows a clear, sequential process to bring the robotic system to life, as illustrated in the diagram above:

1.  **Parse the Config**: The process begins by reading a `.pbtxt` configuration file specified via the `--config` flag. This file contains all the necessary information about the robot's hardware, sensors, actuators, and AI policies.

2.  **Analyze Nodes and Interfaces**: The generator analyzes the configuration to identify all the required node types. It groups these components by their assigned `node_id` and ensures no duplicates or conflicts (e.g., multiple nodes claiming the same serial port).

3.  **Integrity Check for Config**: It performs a series of checks to ensure the configuration is valid and all required information is present. This step prevents runtime errors due to misconfiguration.

4.  **Deploy/Run All Nodes**: Resolve every executable before starting the graph. Each node runs in its own process group, with a small guardian retaining that group's identity. The application gets its own configuration arguments and runfiles environment. Exec failures and readiness failures roll back partial startup.

5.  **Monitor Running Nodes**: An unexpected required-node exit fails the graph and stops its siblings. A trajectory process may complete successfully without failing the graph. Shutdown sends SIGINT, waits three seconds by default, then gives SIGTERM two seconds, followed by SIGKILL and a one-second reaping window. No node is automatically restarted.

## Ownership and modules

- `launch_spec.*` resolves executable paths, arguments, working directories, and environment without spawning processes.
- `process_supervisor.*` owns guardian processes, application PIDs, process groups, startup channels, signaling, and reaping. It does not read robot configs or construct ROS objects.
- `node_generator.*` maps config node IDs to launch specs, collects topic metadata, and applies graph policy on the launcher thread.
- `launcher/joshua_main.cc` owns scoped signal handlers and mode selection. Simulation uses the same supervisor and monitors its accompanying ROS graph.

Each job is `launcher → guardian → application → descendants`. The guardian
reports the actual application PID and wait status, stays alive after application
exit to reserve the process group, and kills its group if its launcher dies.
The launcher enables Linux subreaping and reaps only owned process groups;
unrelated child processes are not consumed through `waitpid(-1)`.
Descendants must remain in their assigned process group. Processes that detach
with `setsid()` or change groups are outside this ownership contract. Abrupt
launcher death forces termination and cannot run application teardown.

Snapshots retain startup state, readiness, application exit code/signal, expected
exit, forced shutdown, errors, and reaping status. `GetLaunchedNodes()` includes
exit history. The application PID and guardian/process-group PID are distinct;
only the supervisor should signal or reap them.

## Readiness and shutdown

Managed C++ nodes send one byte through `JOSHUA_READY_FD` after configuration,
RobotTime setup, node construction, and executor registration. The shared Python
node runner sends the same notification after construction; it does not implement
RobotTime. The startup channel is closed after notification. Readiness means the
node was constructed, not that a lazy sensor has produced its first valid sample.

The inference host and simulation executable have not adopted this protocol;
their snapshots report executable startup separately and never claim readiness.
The whole graph has a 30-second readiness budget for nodes using the protocol.

Signal handlers only set a flag. Normal control flow performs bounded shutdown
and returns a status to the guarded launcher; it never calls deep `exit()`.
OS process grace periods use host elapsed time so shutdown remains available
when RobotTime fails or pauses. Driver transaction deadlines still use RobotTime.
Shutdown is idempotent and retains records after reaping. OS tasks that cannot
be reaped within the kill window are reported as a shutdown failure.

Hardware-free fixtures and process tests are in `process_fixture.cc` and
`process_supervisor_test.cc`; run them only when validation is requested.

## Usage

The Node Generator is executed from the command line, pointing to a specific configuration file.

### Running the Node Generator

Check the [launcher/joshua_main.cc](../launcher/joshua_main.cc)

## Config validation

[config/validation.h](../config/validation.h) exposes `config::ValidateConfig(const config::Config&)` as
an independent library, separate from process launching. `CheckConfigIntegrity`
and preset-validation tests both call it. The C++ ROS node runner also validates
configs before constructing a node when launched directly.

[config/validation.cc](../config/validation.cc) composes focused checks for node assignments,
resource references, serial settings, and bus ownership. The entry point takes
the full config so checks for boards, actuators, sensors, and future sections
have one shared home. Add new checks as separate functions and call them from
`config::ValidateConfig`. Sensor configuration checks and dependency extraction are
separate private helpers in this module. Factories retain defensive checks for
construction and initialization, and do not depend on node generation.
Validation does not instantiate drivers, open devices, or launch nodes.
