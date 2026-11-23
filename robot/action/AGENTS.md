# Actions

Follow [robot/AGENTS.md](../AGENTS.md), the repository instructions, and shared
coding guidelines. This file owns
the local architecture contract and rules for `robot/action/`.

## Architecture contract

Drivers own motor semantics: command combinations, unit conversion, calibration,
operational limits, and actuator-specific enable/disable behavior. Use board
interfaces/channels resolved by ActionFactory and BoardFactory. Do not depend on
Serial, SOEM, concrete transports, ROS callbacks, or MCP.

Add sibling driver implementations behind ActionInterface and the existing
factory registration table. Keep robot values in protobuf configuration. Preserve
explicit field presence, normalized/native/SI position meaning, invalid-command
rejection before writes, and runtime errors. Shared field validation must not
claim support for a driver-specific command combination.

## Verification

Use factory-backed mock channels and existing motor/factory tests. Check supported
and rejected payloads, units/limits, multiple actuators, enable policy, failure
returns, and teardown. Run affected `//robot/action/...` targets in Docker;
software results do not prove physical completion or stopping.

## Actions

Motor drivers translate configured actuator commands into board-channel operations.
Interfaces, protobuf packets, factories, and drivers live here; board protocol
and byte transport live in their sibling layers. The [contract](AGENTS.md) owns
implementation and test rules.

This code can move physical actuators. Follow the root
[hardware authorization rules](../../AGENTS.md#hardware-safety--read-this-first).
