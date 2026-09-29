# Board and Comm Separation Plan

Status: **Proposed**

Companion to: [BOARD_LAYER_RFC.md](BOARD_LAYER_RFC.md),
[am243_ethercat.md](am243_ethercat.md)

## 1. Goal

Separate board protocol behavior from communication resource ownership while
making JoshuaWire available over EtherCAT.

Motor drivers continue to depend only on
[`BoardChannel`](../robot/board/interfaces/board_channel.h). Board code owns
identity, channel configuration, command routing, and protocol semantics. Comm
code owns serial ports, EtherCAT masters, CoE mailbox access, PDO exchange,
timeouts, synchronization, and shared-resource lifetime.

JoshuaWire over EtherCAT uses both native EtherCAT planes:

- `RESET_SESSION`, `IDENTIFY`, `CONFIGURE_CHANNEL`, `ENABLE`, `DISABLE`, and
  `ESTOP` use CoE/SDO mailbox request-response.
- `SET_TARGET` and `GET_FEEDBACK` use correlated request and response slots in
  the cyclic PDO image.

The current TI eight-byte echo demo remains a bring-up artifact. This design
requires Joshua-controlled AM243 firmware and a matching PDO/ESI definition.

## 2. Stable public interface

The refactor does not change
[`BoardInterface`](../robot/board/interfaces/board_interface.h) or
[`BoardChannel`](../robot/board/interfaces/board_channel.h):

```cpp
class BoardChannel {
 public:
  virtual absl::Status Enable() = 0;
  virtual absl::Status Disable() = 0;
  virtual absl::Status SetTarget(TargetMode mode, float value) = 0;
  virtual absl::StatusOr<ChannelFeedback> ReadFeedback() = 0;
};
```

The selected comm determines how those operations travel, but motor drivers do
not see serial, EtherCAT, SOEM, CoE, SDO, or PDO types.

`ESTOP` remains a board-protocol operation rather than a new public method in
this migration. The board engine issues it from fail-safe stop and teardown
paths; cyclic communication loss independently places firmware into its
latched safe state. A user-facing emergency-stop API is separate work and must
not be approximated by downcasting `BoardInterface`.

## 3. JoshuaWire v2 correlation

JoshuaWire v2 adds nonzero, little-endian `uint32_t session_id` and
`uint32_t message_id` fields to every request. Every response echoes both
fields. The session ID is established by the reset handshake below; it is not
a board identity or a value that persists across reboot.

The v2 frame is:

```text
[sync][len][proto_ver][session_id_le][message_id_le][cmd][channel][payload...][crc16_le]
```

The length and CRC cover `session_id` and `message_id` along with the remaining
header and payload. The decoded frame becomes conceptually:

```c
typedef struct {
  uint8_t proto_ver;
  uint32_t session_id;
  uint32_t message_id;
  uint8_t cmd;
  uint8_t channel;
  const uint8_t* payload;
  uint8_t payload_len;
} jw2_frame_t;
```

Protocol rules:

- Before normal exchange, the host sends `RESET_SESSION` with a newly generated
  nonzero session ID. Firmware atomically clears request de-duplication state
  and retained responses, adopts the ID, and echoes it. No channel may be
  configured or enabled until this handshake succeeds. `RESET_SESSION` is the
  sole frame firmware accepts when its proposed session ID differs from the
  active session.
- Firmware reboot resets the active session to zero. Host reconnect always
  performs `RESET_SESSION`, even if transport buffers or PDO inputs retain old
  bytes. Frames from any other session are discarded.
- Serial open/reconnect flushes receive bytes before the handshake. PDO and
  mailbox responses remain explicitly tagged with the session ID, so a
  retained old response cannot match a request in a new session.
- Message ID `0` is reserved and never allocated.
- The host obtains session IDs from an operating-system entropy source. Tests
  inject a deterministic generator. Failure to obtain entropy fails
  initialization rather than falling back to a predictable constant.
- The host allocates IDs monotonically. Before `UINT32_MAX` would wrap, it
  disables channels and establishes a new session, so an ID is never reused
  within one session.
- Mailbox and PDO envelope generation equals the enclosed JoshuaWire message
  ID. There is one allocator, not a second independently wrapping generation
  counter.
- A response must match session ID, message ID, command, and channel.
- Stale, duplicate, or mismatched responses do not complete a call.
- A timed-out ID remains quarantined from late responses until transport state
  has advanced past that response.
- JoshuaWire v1 remains available during migration. Current firmware and
  presets move to v2 only after both endpoints support it; v1 wire bytes are
  not silently reinterpreted as v2.

## 4. Communication seams

There are two comm interfaces rather than one interface containing
transport-specific methods.

### 4.1 Message transport

`MessageTransport` handles acyclic payload exchange. Serial and EtherCAT CoE
are adapters behind this seam. It supports both request-response exchange and
explicit send-only behavior needed by vendor protocols such as Feetech.

Conceptually:

```cpp
class MessageTransport {
 public:
  virtual absl::Status Send(
      absl::Span<const uint8_t> request,
      absl::Duration timeout) = 0;
  virtual absl::StatusOr<std::vector<uint8_t>> Exchange(
      absl::Span<const uint8_t> request,
      absl::Duration timeout) = 0;
};
```

JoshuaWire management commands use `Exchange`. EtherCAT implements it through
CoE/SDO mailbox objects; serial implements it through framed byte-stream I/O.

### 4.2 Correlated cyclic transport

The EtherCAT cyclic adapter exposes correlated command exchange without
leaking PDO regions or SOEM types to the board engine:

```cpp
class CorrelatedCyclicTransport {
 public:
  virtual absl::StatusOr<std::vector<uint8_t>> Exchange(
      absl::Span<const uint8_t> request,
      absl::Duration timeout) = 0;
};
```

Only `SET_TARGET` and `GET_FEEDBACK` are accepted by this adapter. The board
engine rejects any routing table that sends another command through it.

### 4.3 Capability bundle and lease

One configured communication resource may expose more than one interface.
`CommFactory` therefore returns a lease containing optional capabilities
rather than a variant containing exactly one transport:

```cpp
struct CommCapabilities {
  std::shared_ptr<ByteStream> byte_stream;
  std::shared_ptr<MessageTransport> message;
  std::shared_ptr<CorrelatedCyclicTransport> correlated_cyclic;
};

class CommLease {
 public:
  const CommCapabilities& capabilities() const;
};
```

The lease keeps the shared serial port or EtherCAT master alive. Destruction of
one board's lease does not stop a master still used by another board. The
factory rejects incompatible requests for one EtherCAT interface, including
different process-data modes, cyclic periods, mailbox budgets, or recovery
policies.

Configuration declares a set of required transport capabilities rather than
one mutually exclusive `transport_type`. During migration, the existing
singular field remains readable and maps to a one-element requirement set.
EtherCAT JoshuaWire requires both message and correlated-cyclic capabilities.

## 5. EtherCAT runtime model

Each EtherCAT master has one background cyclic loop. It is the only code that
uses the SOEM master context, including process-data exchange, SDO operations,
state checks, recovery, and teardown. Callers enqueue work and wait without
holding a master or adapter mutex; a blocked board call never drives the bus
itself.

```mermaid
sequenceDiagram
  participant Caller
  participant BoardEngine
  participant CyclicAdapter
  participant MasterLoop
  participant Firmware

  Caller->>BoardEngine: SET_TARGET or GET_FEEDBACK
  BoardEngine->>CyclicAdapter: Exchange JoshuaWire v2 frame
  CyclicAdapter->>CyclicAdapter: Reserve request slot
  MasterLoop->>Firmware: Exchange output PDO
  Firmware->>Firmware: Process new message ID once
  Firmware->>MasterLoop: Exchange input PDO response
  MasterLoop->>CyclicAdapter: Publish matching response
  CyclicAdapter->>BoardEngine: Wake waiter
  BoardEngine->>Caller: Return status or feedback
```

The initial implementation allows one in-flight cyclic request per
board/slave adapter. One master loop may service several slave PDO regions.
Concurrent callers targeting the same adapter serialize on its request slot.
The request generation is the JoshuaWire message ID, so generation wrap uses
the same new-session procedure as message-ID wrap.

The worker always performs process-data send/receive first. Between cyclic
deadlines it may service at most one queued mailbox or state-management step.
Each step has a configured hard time budget smaller than the measured cyclic
slack; the SOEM timeout passed to a blocking primitive is capped to that
budget. A configuration is rejected if its mailbox budget leaves no cyclic
slack. If the SOEM primitive cannot honor that bound, mailbox traffic is not
permitted while cyclic channels are enabled until it is replaced by an
incremental or otherwise bounded implementation. Slow and timed-out SDO tests
must demonstrate that cyclic deadlines and watchdog refreshes are preserved.

Stop and ESTOP work has priority over mailbox, recovery, and normal cyclic
requests. Stop prevents new work, wakes pending callers, publishes invalid
outputs, executes the final safe-state exchange when the bus is usable, and
then tears down the master. Locks are acquired only in lifecycle → work-queue
→ per-adapter order. The worker releases all three before any SOEM call or
caller notification and never waits for a caller, eliminating reverse-order
and callback deadlocks.

JoshuaWire v2 frames are bounded to 64 bytes, including framing and CRC. PDO
layout version 1 uses fixed 80-byte output and input images with these
little-endian offsets:

| Direction | Offset | Type | Field |
| --- | ---: | --- | --- |
| Output | 0 | `uint32` | session ID (`0` means no active session) |
| Output | 4 | `uint32` | request generation (`0` means invalid/cancelled) |
| Output | 8 | `uint32` | response generation acknowledgment |
| Output | 12 | `uint16` | request frame length, `0..64` |
| Output | 14 | `uint16` | flags/reserved, must be zero in layout v1 |
| Output | 16 | `uint8[64]` | request frame, zero-filled after length |
| Input | 0 | `uint32` | active firmware session ID |
| Input | 4 | `uint32` | accepted request generation |
| Input | 8 | `uint32` | published response generation (`0` means none) |
| Input | 12 | `uint16` | response frame length, `0..64` |
| Input | 14 | `uint16` | transport status |
| Input | 16 | `uint8[64]` | response frame, zero-filled after length |

The host constructs a complete output shadow image, then the sole master worker
copies that image into the SOEM process image before send. Firmware latches the
complete EtherCAT output image on its process-data receive event and examines
generation only afterward. In the reverse direction, firmware fills the frame
and metadata before publishing response generation; the master worker copies a
complete received process image to an input shadow before waking a waiter.
These process-image snapshot boundaries, not C++ atomics alone, prevent partial
images from crossing the host, controller, and firmware boundaries.

Firmware executes a nonzero `(session ID, request generation)` at most once,
then publishes `accepted request generation`. It retains a response until the
host publishes the matching response-generation acknowledgment. The host keeps
a request valid until it observes acceptance, completion, cancellation, or its
deadline. On timeout it publishes generation zero at the next cycle. Firmware
must not begin an unaccepted request after observing that cancellation, but a
request accepted before cancellation may already have executed; such a timeout
returns an outcome-unknown error rather than reporting that the command did not
run. Late completion is acknowledged and discarded.

Completion requires a newly observed response whose message ID, command, and
channel, session ID, and generation match the active request. Timeout, bus
failure, or teardown clears the active waiter and returns an error. Late
responses are discarded as described above.

The cyclic period and response timeout are configuration values. Working-count
failure, loss of OPERATIONAL state, or a stopped loop fails pending calls.

## 6. EtherCAT mailbox model

The SOEM adapter gains CoE/SDO read and write operations. Joshua-controlled
AM243 firmware exposes object-dictionary entries for request and response
JoshuaWire v2 frames.

The layout-v1 CoE contract is:

| Index | Subindex | Type/access | Meaning |
| --- | --- | --- | --- |
| `0x2000` | `0` | `OCTET_STRING[36]`, RO | compatibility descriptor |
| `0x2010` | `0` | `OCTET_STRING[76]`, WO | mailbox request envelope |
| `0x2011` | `0` | `OCTET_STRING[76]`, RO | retained mailbox response envelope |
| `0x2012` | `0` | `UNSIGNED32`, WO | mailbox response-generation acknowledgment |

The 36-byte compatibility descriptor contains, in order, magic
`"JWEC"` (`uint8[4]`), descriptor version (`uint16`, value 1), minimum and
maximum JoshuaWire protocol versions (`uint16` each), PDO layout version
(`uint16`, value 1), output and input PDO sizes (`uint16`, both 80), maximum
frame size (`uint16`, 64), supported-transport bitmap (`uint32`), and a
12-byte NUL-padded firmware artifact ID followed by a zero `uint16` reserved
field. Supported-transport bitmap bit 0 means JoshuaWire mailbox envelopes and
bit 1 means correlated JoshuaWire PDO images; all other bits are reserved and
must be zero in descriptor version 1. Each 76-byte mailbox envelope contains
session ID (`uint32`), generation (`uint32`), frame length (`uint16`), zero
reserved bytes (`uint16`), and frame (`uint8[64]`). Mailbox generations and
retained-response acknowledgment follow the same publication rules as PDO
generations.

Mailbox exchange is used only for management operations:

```text
RESET_SESSION
IDENTIFY
CONFIGURE_CHANNEL
ENABLE
DISABLE
ESTOP
```

`RESET_SESSION` is an ordinary JoshuaWire v2 request in the `0x2010` envelope,
not a transport-specific side channel. It is the only request firmware accepts
when the envelope and frame propose a session different from the active one.

Only one mailbox request is in flight per slave. The host writes `0x2010`,
then reads `0x2011` until it observes the exact session and generation or the
deadline expires. Firmware publishes the complete retained response before its
generation. After consuming or discarding that response, the host writes the
generation to `0x2012`; firmware then clears it. A zero generation means no
published response. Mailbox timeouts, polling, and serialization belong to the
comm adapter. The board engine sees only a `MessageTransport`.

At startup, before session reset or channel enable, the host reads `0x2000` and
requires JoshuaWire v2, PDO layout v1, exact 80-byte PDO mappings, a 64-byte
frame limit, and the transports required by the selected configuration. A
mismatch fails initialization with the observed artifact ID and expected
protocol/layout/size/transport values, plus an instruction to build and flash
the matching firmware artifact. Firmware is built and flashed separately from
the host; each firmware artifact only needs to implement the transport set
declared in its compatibility descriptor, not every Joshua transport.

`ESTOP` is also reflected as latched firmware safety state. Transport status
in PDO layout v1 is `0` for ready, `1` for malformed envelope, `2` for
unsupported cyclic command, `3` for session mismatch, and `4` for firmware
internal error; all other values are reserved. Loss or staleness of cyclic
traffic must place outputs into the documented safe/disabled state; mailbox
delivery alone is not the motor-safety mechanism.

## 7. Board engine and factory assembly

A composed JoshuaWire board engine uses a transport-specific routing table.
EtherCAT routes `RESET_SESSION`, `IDENTIFY`, `CONFIGURE_CHANNEL`, `ENABLE`,
`DISABLE`, and `ESTOP` through `MessageTransport`/CoE and routes `SET_TARGET`
and `GET_FEEDBACK` through `CorrelatedCyclicTransport`/PDO. Serial routes every
command, including `SET_TARGET` and `GET_FEEDBACK`, through
`MessageTransport::Exchange`; it does not require a cyclic transport.
Message-only boards, including Teensy and ESP32, retain all commands during
migration. Factory validation rejects only commands unsupported by the
selected transport's declared capability set.

```mermaid
flowchart LR
  MotorDriver --> BoardChannel
  BoardChannel --> JoshuaWireEngine
  JoshuaWireEngine --> CommandRouter
  CommandRouter -->|"All commands on serial; management on EtherCAT"| MessageTransport
  CommandRouter -->|"SET_TARGET GET_FEEDBACK on EtherCAT"| CorrelatedCyclicTransport
  MessageTransport -->|"Serial"| FramedSerial
  MessageTransport -->|"EtherCAT"| CoEMailbox
  CorrelatedCyclicTransport --> PDOImage
```

[`CommFactory`](../robot/comm/factory/comm_factory.cc) creates ready-to-use
serial or EtherCAT adapters and owns configure/start/stop, master caching,
serialization, and shared-resource leases. Its returned lease may expose
multiple capabilities from the same underlying resource; it does not require
callers to create separate EtherCAT masters for CoE and PDO.

[`BoardFactory`](../robot/board/factory/board_factory.cc) resolves these axes
independently:

- Board type to board identity.
- Protocol to JoshuaWire or a vendor protocol adapter.
- Comm type to available message and cyclic capabilities.

An EtherCAT JoshuaWire configuration is rejected unless both its CoE message
adapter and correlated PDO adapter are available. Feetech remains a separate
message-protocol adapter.

## 8. Configuration changes

Add explicit board protocol selection so the factory does not reconstruct a
hidden `board_type × comm_type` matrix.

Replace the singular required transport selection with a capability set.
Existing configs using one `transport_type` remain accepted during migration;
new EtherCAT JoshuaWire configs explicitly require message and
correlated-cyclic capabilities.

Move EtherCAT endpoint facts—slave index and optional PDO region overrides—from
AM243 board configuration into
[`EthercatConfig`](../robot/comm/proto/comm.proto). Keep the selected PDO
mapping as board protocol/layout data.

Add:

- EtherCAT cyclic period.
- EtherCAT cyclic response timeout.
- Serial post-open settle delay, replacing the ESP32 board-specific sleep.

## 9. Firmware work

Introduce a Joshua-controlled AM243 EtherCAT firmware/profile rather than
modifying the retained TI demo.

The firmware must:

- Implement the JoshuaWire v2 codec and echo session and request IDs in every
  response.
- Implement reset-session and clear retained/de-duplication state on reboot.
- Expose CoE object-dictionary entries for management requests and responses.
- Consume each new PDO request once.
- Accept only `SET_TARGET` and `GET_FEEDBACK` through the PDO command slot.
- Publish a correlated PDO response.
- Define stale-target and communication-loss watchdog behavior.
- Latch disabled/safe state after ESTOP or cyclic communication loss.
- Share packed PDO definitions with the host or verify them with size and
  offset assertions and golden-layout tests.

Document or generate the matching ESI and PDO mapping.

## 10. Stacked implementation PRs

The design update is PR 0. Implementation is split into nine reviewable PRs.
Every PR keeps tests green and preserves the retained TI demo.

1. **Communication seams and configuration.** Finalize `MessageTransport`,
   `CorrelatedCyclicTransport`, capability leases, fakes, configuration
   migration, and BUILD visibility. Adapt existing users without changing wire
   behavior.
2. **JoshuaWire v2 codec and correlation primitives.** Add the shared v2 C
   codec beside v1, session/message-ID allocation, exact golden bytes, response
   matching, and shared mailbox/PDO layout assertions. No endpoint switches
   protocol in this PR.
3. **Composed board engine and serial v2 host.** Add board identity as data,
   protocol selection, command routing, the reset handshake, and deadline-based
   serial framed reads. Keep explicit v1 support.
4. **Serial firmware and board migration.** Add v2 to Teensy, ESP32, and AM243
   serial firmware; propagate response IDs; move serial settle delay into comm
   config; migrate factory assembly; and remove Teensy/ESP32 board subclasses.
   This PR completes the serial-v2-stable milestone.
5. **Single-owner EtherCAT runtime.** Add the background master loop, bounded
   work queues, lifecycle, shared leases, stop priority, teardown cancellation,
   and timing tests. Keep the TI demo usable through a compatibility adapter.
6. **CoE/SDO mailbox transport.** Add worker-owned SDO operations,
   compatibility validation, mailbox publication/acknowledgment, reset-session
   exchange, and slow-mailbox deadline tests.
7. **Correlated PDO transport.** Add the 80-byte host codec, process-image
   shadows, generation publication and acknowledgment, cancellation,
   outcome-unknown timeout handling, and cyclic routing tests.
8. **Joshua-controlled AM243 firmware/profile.** Add the CoE object dictionary,
   correlated PDO handling, watchdog and ESTOP safe state, ESI/PDO mapping, and
   host/firmware layout tests. This remains a separate artifact from the TI
   demo.
9. **EtherCAT integration and cleanup.** Compose mailbox and PDO capabilities
   in `BoardFactory`, apply the compatibility gate, migrate AM243, remove
   `Am243Board::serial_mode_` and board dependencies on serial/SOEM, and run
   both Compose CI task services.

PRs 1–4 are the host/serial spine. PRs 5–9 form the EtherCAT stack. PR 8 may be
developed in parallel from PR 2's frozen wire contract, but lands before PR 9.

## 11. Verification

Required host and shared-codec tests:

- JoshuaWire v2 exact golden bytes and CRC coverage.
- v1/v2 compatibility and explicit version rejection.
- Every response echoes its session and request IDs.
- Message-ID exhaustion starts a new session rather than reusing an ID.
- Reboot, reconnect, retained PDO/mailbox response, and reset-session behavior.
- One-in-flight serialization.
- Stale, duplicate, wrong-command, and wrong-channel response rejection.
- Timeout and late response after timeout.
- Bus failure and teardown cancellation.
- EtherCAT management commands route only to mailbox.
- EtherCAT `SET_TARGET` and `GET_FEEDBACK` route only to PDO.
- Host and firmware PDO size/offset agreement.
- CoE index/type, frame-limit, protocol/layout compatibility-gate, and
  actionable firmware-artifact error tests.
- Published-request cancellation before acceptance and outcome-unknown timeout
  after acceptance.
- Slow and timed-out mailbox operations do not violate cyclic deadlines.
- Only the background master loop performs process-data exchange.
- Shared EtherCAT master lifetime across multiple board/slave adapters.
- One EtherCAT lease exposes mailbox and correlated-cyclic capabilities
  without opening a second master; incompatible shared-master timing requests
  are rejected.
- Serial routes all commands through message exchange; Teensy and ESP32 do not
  require a cyclic transport.

Run focused Bazel tests during migration, format BUILD files with buildifier,
then run both Compose CI task services:

```bash
docker compose run --rm test-u22
docker compose run --rm test-u24
```

Tests and builds do not require attached hardware. Do not run a hardware preset
or flash firmware without explicit hardware confirmation.

## 12. Acceptance criteria

- Motor drivers remain unchanged and depend only on `BoardChannel`.
- JoshuaWire v2 works over serial with correlated responses.
- Session reset prevents pre-reboot, pre-reconnect, and retained responses from
  satisfying new requests.
- EtherCAT management commands use CoE/SDO.
- EtherCAT `SET_TARGET` and `GET_FEEDBACK` use correlated PDO slots.
- One EtherCAT communication lease supplies both planes from one master.
- One background loop exclusively owns each EtherCAT master's process-data
  exchange.
- Timeout, teardown, stale responses, and message-ID wrap are deterministic
  and tested.
- PDO/CoE layouts, frame limits, supported versions, and publication and
  acknowledgment rules are exact and compatibility-gated before channel
  enable.
- Serial message-only boards retain all JoshuaWire commands.
- Board targets cannot name concrete serial or SOEM implementations.
- The retained TI demo remains independently runnable for bring-up and is not
  mistaken for the JoshuaWire EtherCAT firmware.
