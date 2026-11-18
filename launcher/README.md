# Launcher

The launcher loads an explicitly selected protobuf preset and delegates configured
ROS nodes and simulation processes to [NodeManager](node_manager/README.md).
That guide documents readiness, process groups, shutdown, and passive observations.
The directory [contract](AGENTS.md) defines implementation boundaries.

A normal launch can open physical devices and move motors. Follow the root
[hardware authorization rules](../AGENTS.md#hardware-safety--read-this-first).
