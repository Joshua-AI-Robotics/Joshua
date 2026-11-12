# Runtime observation

Process configuration, launch, monitoring, and shutdown are owned by
[`launcher::NodeManager`](../launcher/node_manager/README.md).
This directory retains the passive `RuntimeObservation` adapter and its
hardware-free tests and fixture.

For mixed JoshuaWire MCU use `MCU_BUS` for every actuator and scalar perception
on that board, with one shared node ID. The generator launches `ros2/mcu_bus`
once for that ID; configuration validation rejects competing process owners.
The launcher packages that binary along with the standalone node binaries.

The launcher accepts `--observation_dir` and optional `--observe_sensor` flags.
NodeManager validates the selection and acquires an exclusive lease on an
existing private directory before launching children. It starts a private ROS
context after all forks, without installing global ROS signal handlers.
Process monitoring runs before bounded observation work; initialization,
callbacks, graph queries and snapshot publication failures stay contained.

Snapshots describe the launcher's loaded config, process facts, retained exits,
bounded partial `/rosout` logs and one explicitly selected POSITION/JOINT_STATE
stream. Log names do not prove runtime association. Feedback requires the sole
expected publisher endpoint and actual message publisher GID, and cached data
is invalidated when the endpoint set changes. Receipt freshness does not prove
acquisition freshness or physical readiness. Simulation observation is
unsupported. The [MCP guide](../tools/mcp/README.md#optional-runtime-observations)
documents directory permissions, clock domain, fields and limitations. MCP is
an optional reader; NodeManager remains the runtime owner.

`DEVICE_BUS` similarly launches `ros2/device_bus` for shared board-backed actuator
and position/scalar consumers. The launcher includes this binary in its runfiles
and release package. Conflicting process ownership remains a preflight error.
