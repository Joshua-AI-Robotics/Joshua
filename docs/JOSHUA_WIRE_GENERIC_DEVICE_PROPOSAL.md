# JoshuaWire generic device contract — draft reference

Status: **proposal and executable reference**, stacked on PR #105 at `d99a77b`.
The command/profile IDs here are provisional. No runtime, preset, production
firmware target, or existing wire payload selects this extension.

JoshuaWire is the foundation for the board layer and subsequent stacked PRs.
It must describe a firmware endpoint serving motors, sensors, and I/O together.
Supporting a new device should require a profile/backend, not a new framing
codec or a new board enum threaded through the host. A platform participates
by implementing the core contract and selected profiles; flashing remains a
separate platform-specific operation.

## Reference artifacts and adoption

- [`device_contract.h`](../firmware/reference/joshua_wire_device/device_contract.h)
  and `.c`: proposed discovery and observable-state types with explicit codecs.
- [`mixed_device_test.cc`](../firmware/reference/joshua_wire_device/mixed_device_test.cc):
  fake firmware using the existing complete-frame endpoint and session.
- `//firmware/reference/joshua_wire_device:mixed_device_test`: hardware-free test.

The library is `testonly` and lives outside `firmware/common`, whose PlatformIO
source filter automatically compiles production sources. The existing JW1/JW2
definitions and consumers retain their bytes and behavior. This proposal can
be reviewed/cherry-picked independently before a coordinated migration.

The reference demonstrates protocol shape, not a production scheduler, safety
controller, sensor driver, boot-ID allocator, or universal profile registry.
The fake pin map and profile IDs are test fixtures only. Production wiring and
settings still come from the protobuf config.

## 1. Logical functions and profile discovery

A function has a stable channel address and one or more versioned profiles.
Several functions may belong to one physical device. For example, a motor and
its temperature sensor share a physical-device ID; the motor function also
offers a diagnostic profile. No exclusive actuator/sensor union is needed.

`JWD_INFO` returns the number of **descriptor entries**, not a motor count.
`JWD_DESCRIBE` takes a u16 ordinal and returns one `(channel, profile)` entry.
Ordinal is only a discovery cursor; it is never the operational channel ID.
The table is immutable within one discovery/session epoch. Dynamic changes
require an explicit invalidation/re-discovery contract before implementation.
Absent devices retain their function addresses and report unavailable state.
Unknown optional profiles can be skipped; missing configured requirements fail
initialization explicitly. Vendor/product identity is descriptive, while
profile/version support determines compatibility.

The existing frame has a one-byte channel field with `0xff` reserved for board
scope: at most 255 function addresses, independently of the old eight-drive
IDENTIFY layout. This draft does not pretend to remove that address-width
limit. Wider addresses need a negotiated future envelope. Descriptor count is
u16 because one function can expose several profiles.

## 2. Proposed payloads and compatibility

All integers are little endian. Struct padding is never serialized. Every
extension response is `[status:u8][body...]`; non-OK responses have no body.
Unknown status codes are errors, never successful data. Existing RESET, ESTOP,
and other legacy commands keep their existing response formats.

| Operation | Request | Successful response body |
| --- | --- | --- |
| `JWD_INFO` (`0x20`) | Board scope, empty | contract:u8, wire:u8, max-payload:u8, channel-width:u8, descriptor-count:u16, vendor:u32, product:u32, firmware-revision:u32, boot-id:u32 |
| `JWD_DESCRIBE` (`0x21`) | Board scope, ordinal:u16 | channel:u8, physical-device:u16, profile:u32, profile-version:u16, operations:u32, safety-group:u16 |
| `JWD_STATUS` (`0x22`) | Function scope, empty | channel:u8, state:u8, reason:u8, stop-evidence:u8, recovery:u8, generation:u32, safety-group:u16 |
| `JWD_INVOKE` (`0x23`) | Function scope, profile:u32, profile-version:u16, operation:u8, data[] | Profile-defined body |

The largest discovery body is 22 bytes, or 23 with status, within the current
49-byte payload limit. Describe/status responses are 16/12 bytes including
status. Invoke leaves 42 request bytes and 48 successful response bytes for
profile data. Oversize data must be rejected, never silently truncated.

Production adoption must allocate IDs and define an explicit feature probe or
firmware compatibility gate on every transport. A timeout is not evidence that
a peer is legacy; never silently downgrade following an ambiguous operation.
Hosts must validate contract/wire versions and bounds before enabling outputs.
Profile versions are exact matches in this example; compatible ranges require
an explicit future rule. No current host should send experimental IDs without
prior knowledge that this reference contract is implemented.

Larger calibration blocks and sensor arrays need a separately negotiated,
bounded transfer profile: transfer ID, total-size cap, offset/sequence checks,
timeout/cancellation, and atomic commit semantics for configuration. Streaming
and events similarly require distinct message semantics; they cannot consume
request IDs or impersonate replies. These mechanisms are reserved design work,
not implemented by adding a capability bit here.

## 3. One MCU, one session owner

The existing session is ordered and single-flight, with one highest message ID
and one cached response. All host drivers sharing an MCU must share one owner
for session lifecycle, ID allocation, and request scheduling across transports.
Allocate IDs in dispatch order. Queued requests must not overtake each other
after allocation. A sensor-driver reconnect cannot independently reset a board
while actuator drivers continue using its old session.

A lost response may be retried only with the exact original frame/ID. Once a
new request executes, the older cached reply is unavailable, though the older
request still cannot execute again. A timeout then represents **unknown
execution outcome**. Do not generate a new ID and blindly repeat a stateful
operation. Reconcile device state or use a profile-defined operation token/result
query before retrying. Session reset clears history; no automatic replay of
old state-changing operations into a new session is permitted.

Multiple independent host controllers are not supported by this single-owner
contract. Their arbitration/lease ownership must be designed before enabling
them. Session IDs provide correlation, not authenticated ownership.

## 4. Bounded work and shared resource configuration

Firmware dispatch must not wait for a slow conversion, a queue slot, or an
unbounded bus transaction. A sensor read returns a cached sample or NOT_READY;
acquisition runs independently. The fake demonstrates this pattern, not timing
guarantees. Production admission control must bound queue sizes, bus work, and
control/stop latency; reject unsupported requested rates rather than overcommit.

Prioritize stops before allocating the next normal request ID. A stop cannot
simply overtake an already-issued lower ID without cancellation/reconciliation.
Profiles/backends own watchdogs and safe outputs if communication/dispatch
cannot deliver a stop in time. The synchronous endpoint is not a physical
emergency-stop guarantee.

Before applying configuration, validate the entire change against pin, timer,
ADC, and bus ownership. Shared bus access belongs to one backend owner. Commit
only a valid configuration or report an explicit recovery state after a failure.
The fake proves rejection of a conflicting pin without changing either channel.
Timer groups and multi-channel atomic configuration belong to actual backends.
Synchronized multi-axis motion likewise needs a profile with explicit grouped
commands/commit timing; sequential single-channel writes do not imply simultaneity.

## 5. Safety scope and host-visible state

Safety groups advertise shared shutdown domains. The reference has two groups;
board ESTOP affects both, including their sensors. Equal nonzero group IDs
indicate shared shutdown effects, not a complete arbitrary dependency graph.
Richer topologies require a paginated dependency profile before deployment.

After ESTOP, queryable state identifies each function, its availability, reason,
stop evidence, and required recovery. The fake reports motors disabled and
sensors unavailable **because of ESTOP**. Firmware output-disable evidence does
not claim verified physical standstill. Restoring a sensor cannot enable a motor;
motor configuration and explicit enable are separately required.

Status queries report a board-wide generation. To reconcile several functions,
the host requires equal generations and a stable boot identity across the read;
otherwise it retries. Production must define generation exhaustion and ensure
boot identities do not repeat across the host's stale-data window. The fixed
boot ID in the fake is not an allocator implementation.

Request acknowledgment, completed firmware stop actions, and physically verified
safe state are separate facts. If communications disappear, the host marks state
unknown/unreachable, never assumes successful stopping. Upon reconnect, reconcile
boot/session identity and all relevant function states before resuming operations.
Events are optional notification; queryable state is the recovery mechanism.
A channel sensor fault normally remains local unless a configured safety
dependency requires stopping another function. Board faults may affect all.

## 6. Measurement semantics

Each sensor profile must define units, field presence, validity, and freshness.
Receiving a new response is not proof of a new sample. Production profiles need
sample sequence/time plus clock domain, tick scale, wrap, and reboot semantics.
No clock synchronization is implied by a timestamp. Session/boot changes must
invalidate caches or explicitly mark retained samples stale.

The test temperature profile intentionally returns a single integer degree-C
value after an externally completed conversion. It demonstrates dispatch only;
it is not the production measurement schema. Reset clears its ready flag so
that a previous-session sample cannot be silently reused.

## 7. Work split and acceptance

| Layer / follow-up | Required responsibility |
| --- | --- |
| Shared protocol foundation | Review/allocate discovery, profile, status and error contracts; separate common command/types from actuator payloads; define migration gate |
| Host board engine | One shared owner/scheduler, required-profile validation, unknown-outcome reconciliation, boot/state tracking |
| Firmware backends | Typed profiles, bounded acquisition, atomic resource validation, watchdog/safety implementation and evidence |
| Transport adapters | Bounds, deadlines, cancellation, cross-plane ordering and disconnect notification |
| Integration tests | Real scheduler contention, timeout outcome handling, deadline budgets, timer/bus conflicts, reconnect/boot behavior |

The reference exercises ten functions, multiple profiles per function, shared
physical identity, pin conflicts, unsupported profiles/versions, nonblocking
NOT_READY, unavailable sensors, observable board ESTOP, reset invalidation,
identical retries, older retries, and out-of-order rejection. Codec tests pin
independent golden bytes and validate truncation/null/buffer bounds.

It does not prove concurrent scheduling, physical safety, sensor timing, actual
transport reconnect behavior, or hardware compatibility. Those acceptance tests
belong to the implementing stacked PRs; they must not be inferred from this fake.

Run the repository CI task from this checkout:

```bash
docker compose run --rm test-u22
docker compose run --rm test-u24
```
