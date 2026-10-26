# Shared JoshuaWire engine

`JoshuaWireBoard` supplies identity checks, channel configuration, correlated
session reset, command dispatch and safe teardown. `JoshuaWireSession` owns
message IDs and response matching; communication adapters own device I/O and
timeouts. Native integration tests run the production command handler and
firmware endpoint, with no physical device.

ESP32, Teensy and the AM243 serial binding use this engine directly with
`message_adapter: JOSHUA_WIRE`. All Joshua-owned MCU images compile the current
shared endpoint; the old protocol codec, host board, frame wrapper and test
fakes are removed. `CallerSizedSerialTransport` serves vendor protocols such as
Feetech and is not a JoshuaWire compatibility path.

Native `firmware_binding_test` exercises the real factory, serial adapter,
endpoint and physical STEP/DIR backend over PTYs for every migrated host binding.
Only GPIO/clock are simulated. It covers disabled startup, motion commands,
reply loss, pin ownership, stop/reset/recovery and rejection of retired frames.

The engine uses JoshuaWire 0.2.0 / revision 4. Compatibility comes from paged
function discovery and versioned profile metadata, not vendor/product IDs.
Initialization validates dependencies, brackets metadata with IDENTIFY, queries
all function and board states, then applies desired STEP/DIR configuration.
Reset preserves an ESTOP latch; recovery and enable are explicit operations.
Actuator commands use PROFILE and decode the common status/execution prefix.
Only actuator v1.0 target/feedback operations use a paired cyclic capability.

Lost or malformed replies get one exact-byte retry. An unresolved outcome gates
mutations until explicit reset/reconciliation; status queries and stop remain
available. ID exhaustion stops the board and requires reinitialization without
replaying settings or enabling outputs. Feedback validates acquisition validity,
boot/clock, sequence continuity and local cached-sample age.

## Supported scope and streaming follow-up

The current stack supports synchronous serial request/response, including polling
cached feedback. It does not provide continuous unsolicited sensor streaming or
the complete mixed-device priority scheduler. An MCU continuously publishing IMU
samples while actuator commands and ESTOP share that connection remains a future
integration use case.

`MessageTransport` exposes `Send`/`Exchange`; the serial request path discards
pending input before an exchange. Firmware pauses receive processing when its
application response slot is occupied. The bounded serial TX local-fault path
disables outputs if transmission stalls, but does not implement a continuous
receiver, telemetry routing or the full control/stop scheduler.

The generic runtime catalog supports independently registered profiles; runtime
motor bindings still select STEP/DIR. Profile codecs and frame interfaces do not
complete the streaming use case. Adding ordinary Ethernet or CAN adapters alone
does not add subscriptions, and EtherCAT cyclic actuator exchange is not a
generic sensor subscription API. General sensor/I/O driver bindings and the
remaining scheduler/stop-preemption integration gates in the
[protocol contract](../../../docs/JOSHUA_WIRE.md#stacked-implementation-and-integration-gates)
remain follow-up work.

[Tracked follow-up #135](https://github.com/Joshua-AI-Robotics/Joshua/issues/135)
defines acceptance criteria for one receive owner routing replies and samples,
subscription lifetime and sample identity/sequence/timestamp/validity metadata,
reboot/reconnect handling, bounded buffering with an explicit overflow policy,
background acquisition, reserved control/stop capacity and bounded scheduling.
It also requires stream semantics above link-specific adapters, any necessary
framing/reassembly below them, and mixed sensor/actuator integration tests for
overflow, backpressure, correlation and stop handling on the same connection.

## Manual serial diagnostic

The manual `joshua_wire_smoke` CLI runs handshake, disabled configuration or
explicitly authorized exercise modes through the production session. Preflight
validates the selected JoshuaWire serial adapter without opening a device;
`--dry_run` and PTY tests exercise the CLI without physical hardware.
