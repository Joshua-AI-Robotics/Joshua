# Boards

Follow [robot/AGENTS.md](../AGENTS.md), the repository instructions, and shared
coding guidelines. This file owns
the local architecture contract and rules for `robot/board/`.

## Architecture contract

Boards own protocol codecs, device/channel mapping, shared sessions, acquisition
coordinator caches, and board-level safety/recovery. BoardFactory owns configured
instance resolution and caching. Consume CommFactory's transport interfaces;
keep NIC/serial workers and raw I/O below this boundary. Motor semantics belong
in action drivers and ROS orchestration belongs in component nodes.

Reuse the common JoshuaWire engine and portable firmware contract when adding a
board. One connection has one session/coordinator and receive consumer. Preserve
request correlation, session/boot identity, command uncertainty, resource claims,
and deliberate recovery. Cancellation does not retract bytes already sent, and
an acknowledgment does not establish physical stopping.

## Verification

Use fake transports and existing factory, session, coordinator, and native
host/firmware tests. Check malformed/mismatched replies, shared ownership,
timeouts, reset/ESTOP/recovery, telemetry freshness, and rollback when affected.
Run affected `//robot/board/...` targets in Docker; no firmware flashing is needed.

## Boards

Boards adapt configured channels and device protocols to transport capabilities.
The factory resolves and shares board instances; interfaces define what action
and perception consumers can use. See [AGENTS.md](AGENTS.md) for the architecture
contract and [JoshuaWire](joshua_wire/AGENTS.md) for the shared engine.

Boards open real communication links and can enable motors. Follow the root
[hardware authorization rules](../../AGENTS.md#hardware-safety--read-this-first).

## Directory navigation

- [joshua_wire/](joshua_wire/AGENTS.md)
