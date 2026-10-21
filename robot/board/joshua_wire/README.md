# Shared JoshuaWire engine

`JoshuaWireBoard` supplies identity checks, channel configuration, correlated
session reset, command dispatch and safe teardown. `JoshuaWireSession` owns
message IDs and response matching; communication adapters own device I/O and
timeouts. Native integration tests run the production command handler and
firmware endpoint, with no physical device.

During this intermediate migration, existing MCU consumers still use
`LegacyJoshuaWireBoard`. The next stage switches their firmware and board
bindings together. The correlated engine can already be instantiated directly
with message or paired capabilities. `JoshuaWireSerialTransport` assembles
complete frames over the serial mechanism with `message_adapter: JOSHUA_WIRE`.
`LegacyJoshuaWireBoard` and vendor consumers select the separate
`CallerSizedSerialTransport` with `message_adapter: CALLER_SIZED`.

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

The generic runtime catalog supports independently registered profiles. Runtime
motor bindings still select STEP/DIR. General sensor/I/O driver bindings and the
full bounded priority scheduler/stop-preemption gate in the protocol contract
remain follow-up work; serialized synchronous calls do not establish that gate.
