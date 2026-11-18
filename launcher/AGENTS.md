# Launcher

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../docs/CODING_GUIDELINES.md). This file owns the rules and architecture
contract for `launcher/`; child directories inherit it unless they add local rules.

## Architecture contract

One NodeManager owns configured application processes and simulation: launch
records, guardians, process groups, readiness channels, monitoring, shutdown,
and reaping. Applications own their ROS objects and drivers. Passive observation
reads process facts and ROS data; MCP is an optional reader, not a second owner.
Keep the manager implementation and its plain records together rather than
introducing lifecycle wrappers or another supervisor.

Load and validate explicit configuration before constructing hardware resources.
Preserve startup rollback, shared runfiles resolution, unexpected-child failure,
and retained exit status. Readiness means constructed/registered, not a valid
sensor sample. Process deadlines use host elapsed time independently of RobotTime.
Document escalation bounds and report failure to reap; killing a process does
not guarantee physical stopping or destructor cleanup.

## Verification

Use CLI tests, NodeManager's test child, and runtime observation fixtures in
[BUILD](BUILD) and [node_manager/BUILD](node_manager/BUILD). Exercise partial
startup, signals, descendants, readiness failure, cancellation, and escalation
without opening devices. Test both supported ROS stacks when changing execution.

## Launcher

The launcher loads an explicitly selected protobuf preset and delegates configured
ROS nodes and simulation processes to [NodeManager](node_manager/AGENTS.md).
That guide documents readiness, process groups, shutdown, and passive observations.
The directory [contract](AGENTS.md) defines implementation boundaries.

A normal launch can open physical devices and move motors. Follow the root
[hardware authorization rules](../AGENTS.md#hardware-safety--read-this-first).

## Directory navigation

- [node_manager/](node_manager/AGENTS.md)
