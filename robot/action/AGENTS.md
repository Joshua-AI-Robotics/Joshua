# Actions

Follow [robot/AGENTS.md](../AGENTS.md), the repository instructions, and shared
coding guidelines. Read [README.md](README.md) before editing. This file owns
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
