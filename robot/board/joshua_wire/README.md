# Shared JoshuaWire engine

`JoshuaWireBoard` supplies identity checks, channel configuration, correlated
session reset, command dispatch and safe teardown. `JoshuaWireSession` owns
message IDs and response matching; communication adapters own device I/O and
timeouts. Native integration tests run the production command handler and
firmware endpoint, with no physical device.

Declare one board name per JoshuaWire serial MCU and reference its channels
from all consumers. BoardFactory rejects a second name for an already-owned
JW serial port before starting another session or resetting existing outputs.
This reservation follows the board cache lifetime, including explicit teardown.
Ownership matches device identity across path aliases such as `/dev/serial/by-id`
symlinks. A second name is rejected before any session reset; the same board name
and settings through another alias reuse the existing instance.

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
Normal session close uses a fresh session reset to disable outputs and release
firmware configuration/pin claims without creating an ESTOP latch. A later
startup can configure disabled channels again; genuine ESTOPs still require
explicit recovery.
Close invalidates retained host calls even when the exchange fails or throws.
Closing an already-faulted coordinator waits for its bounded disabled reset
instead of cancelling that teardown.
Actuator commands use PROFILE and decode the common status/execution prefix.
Only actuator v1.0 target/feedback operations use a paired cyclic capability.

Lost or malformed replies get one exact-byte retry. An unresolved outcome gates
mutations until explicit reset/reconciliation; status queries and stop remain
available. Confirmed completion of a board-wide stop or recovery retires canceled
pending function tokens; it does not resolve a lost command's historical outcome.
ID exhaustion stops the board and requires reinitialization without
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

## Shared duplex coordinator (P3b)

`JoshuaWireCoordinator::Acquire` shares one queue and existing `JoshuaWireSession`
per `FrameStreamTransport` lease. One reset occurs on first acquisition; retained
closed/faulted leases never reopen. The session remains the sole ID allocator,
correlator, exact-retry and uncertainty owner. An exchange hook supplies duplex
I/O with typed publication progress and one total budget across all attempts.
Raw/vendor and paired cyclic paths keep the existing session behavior.

Ordinary admission is bounded by operations and reserved request/reply bytes.
ESTOP has a separate single slot and retires an ordinary reply wait before its
higher-ID frame is dispatched. A partially transmitted frame is never interrupted
to append a stop. A requested stop installs a host interlock: queries and recovery
remain possible, while further mutations require deliberate disabled reset /
reconnect. A stop acknowledgment is not physical stopping evidence.

Priority is ESTOP, actuator target/disable, configuration, then polling. After the
configured priority burst, lower-priority work can consume slack only when its
complete remaining budget plus guard fits ahead of higher-priority deadlines.
Infeasible load expires or is rejected; this scheduler does not promise arbitrary
producer rates. Queued cancellation/expiry does not publish bytes or consume IDs.
Cancellation after publication remains uncertain, and a retired ID is never
retried. Late replies and samples cannot satisfy a different command.

One protocol worker per connection routes frames during command waits and while
idle. It does not invoke application callbacks. The current notification drain is
explicitly a diagnostic raw-envelope cache, bounded and keyed by session, token
and function. It is not a usable typed sensor API: subscription ownership,
boot/schema/sequence/freshness validation and recovery belong to P3c.

`Board.joshua_wire_host_policy` resolves effective software budgets. Existing
runtime board consumers are connected to this coordinator in P5; no preset is
migrated by P3b. Memory-link and production serial/PTy fixtures exercise the same
session/coordinator with no physical devices.

## Shared typed subscriptions and reconnect (P3c)

`Subscribe(function_id, period_us)` negotiates IDENTIFY, a consistent paged
catalog and the exact sensor schema, bracketed by boot/catalog identity. It
requires sensor 1.1 operation support, scalar-f32, known units/clock, and an
explicit aggregate telemetry byte reservation. Compatible consumers share one
firmware token and period; a conflicting period is rejected. Management mutex
wait, discovery and creation share a finite complete budget.

Each handle owns its lifetime. Closing one consumer preserves others. Last close
retires its cache immediately, then sends a bounded unsubscribe. A lost create or
cancel acknowledgment retains the uncertain identity; no replacement is created.
`QuerySubscription` remains available and does not clear uncertainty. Failed
teardown is reported and requires deliberate disabled reconnect. Mutating stream
acknowledgments are validated inside the existing session before another ordinary
mutation can be admitted.

`Latest()` preserves schema/unit, session/token/function, device boot/clock,
acquisition sequence/time, device omission count and original host receipt time.
Sequence gaps and cache overwrites are observable. Repeated acquisitions do not
renew receipt age; regressions, altered same-sequence values, wrong clocks and
unknown schemas/units are rejected. Invalid measurements (including allowed NaN)
remain observable with `usable=false`. A matching stream's boot change faults the
whole coordinator and invalidates every handle.

`receipt_age`/`receipt_fresh` describe host receipt age. Device acquisition time
stays in its advertised clock; this API does not invent a mapping to host/ROS time
or certify absolute acquisition age. Physical transport latency, synchronization
and freshness claims require the selected fixture's qualification.

`Reconnect` first retires and releases the old physical lease even when old
handles remain referenced. The caller selects the intended endpoint and supplies
identity/catalog reconciliation, which runs with mutations interlocked. It then
returns a fresh disabled session; targets, enables and subscriptions are never
replayed. Old handles remain unavailable. Production session IDs are unique
within the process until exhaustion; custom test sources must provide fresh IDs.
