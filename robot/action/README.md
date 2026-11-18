# Actions

Motor drivers translate configured actuator commands into board-channel operations.
Interfaces, protobuf packets, factories, and drivers live here; board protocol
and byte transport live in their sibling layers. The [contract](AGENTS.md) owns
implementation and test rules.

This code can move physical actuators. Follow the root
[hardware authorization rules](../../AGENTS.md#hardware-safety--read-this-first).
