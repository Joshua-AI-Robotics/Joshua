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

## Selection and channel contract

Motor, board, communication, and drive are independent configuration axes:
`Actuator.motor_type`, `Board.board_type`, `Board.comm`, and `Channel.drive`.
ActionFactory and PerceptionFactory resolve named boards/channels through the
shared BoardFactory; ValidateMotorChannel rejects incompatible motor/drive pairs.
One board instance and communication lease serve compatible local consumers.

BoardInterface owns initialization, channel lookup, and teardown. BoardChannel
owns enable/disable, native-unit target operations, and feedback. Motor drivers
own conversion into those native units. Binary torque-enable registers use
Enable/Disable; a genuine continuous torque target uses `TargetMode::kTorque`.
Unsupported modes return an explicit error instead of reinterpreting their meaning.

For Joshua-owned STEP/DIR firmware, GPIO pins, polarity, pulse width, and limits
come from host protobuf configuration. Firmware advertises supported channel
slots and validates resource claims; discovery cannot establish physical wiring.
Board identity is distinct from transport peripherals and actuator drive hardware.

## Feetech write recovery

At identification, read each servo's status-return level (register 8). Level 1
writes must consume and validate the acknowledgment before releasing the bus.
Level 0 writes are send-only. Preserve the configured reply policy and never
retry a write automatically.

An uncertain write faults the whole board. Reject motion, enable and feedback
so a late reply cannot confirm a later command. Torque disable may still send
best-effort bytes and must report the outcome as unconfirmed. To recover, secure
the mechanism, inspect the link, close all consumers and restart the runtime.
Host `recover` cannot clear this fault. Restart after an external reply-policy
change. See the [vendor protocol](https://github.com/ftservo/FTServo_Arduino/blob/main/src/SCS.cpp)
and [register map](https://docs.waveshare.com/Memory_Map_Explanation/ST_Servo_Memory_Map_Explanation).

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
