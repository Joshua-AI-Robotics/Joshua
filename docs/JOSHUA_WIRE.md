# JoshuaWire protocol contract

JoshuaWire **0.2.0 / wire revision 4** is the shared, transport-independent
contract for MCU boards with mixed actuators, sensors and I/O. This document is
normative for the shared codec and the remaining stacked implementations.
Revision 4 supersedes the in-development revisions 2/3; it retains their frame
shape and 96-byte bound but changes discovery and all response payloads. The
separate JW1 codec and existing hardware consumers retain their current bytes.
No firmware is flashed or runtime board migrated by this change.

## Responsibility boundaries

| Owner | Required responsibilities |
| --- | --- |
| Core codec/session/endpoint (this PR) | Bounded complete frames, CRC, correlation, one MCU session, exact duplicate replay, core command scope, common reply/error schemas, discovery and state schemas. No device I/O. |
| Versioned profiles (this PR's initial codecs) | Operation IDs, payload/metadata types, units, acquisition validity/freshness, configuration schema and semantic validation. Extensible independently of framing/session. |
| Shared host coordinator (stacked PR H below) | One queue, ID allocator and in-flight slot for every driver and transport of one MCU; priority, deadlines, retry policy, compatibility checks and reconciliation. Drivers submit semantic profile operations. |
| Board firmware/backends (stacked PR F) | Stable function/device catalog, bounded dispatch, background acquisition, resource arbitration and staged hardware configuration, safety implementation and authoritative queryable state. |
| Transport adapters (stacked PR T) | Bounded framing/reassembly, transport availability/cancellation/deadlines, flushing retained data, MCU identity binding, cross-plane publication order and watchdog service. They do not allocate independent session/message IDs. |
| Host protobuf config | Intended wiring, settings, enabled functions, and policy. Discovery describes what exists/can operate; it never replaces desired configuration. |

A new device profile adds a separate payload codec and host/backend dispatch
registration. The core treats profile IDs and operation data as opaque; adding
it requires no board enum, framing change, or session algorithm change.
`jw_drive_t` remains actuator-only metadata. A transport may carry existing
profiles on a new board without adding its vendor/product to a core enum.

## Envelope and limits

All multibyte integers are little-endian; floats are IEEE-754 binary32.
Semantic C structs are not packed wire representations.

```text
[sync:a5][length:u8][revision:4][session:u32][message:u32]
[command:u8][function:u8][payload...][crc16:u16]
```

Length and CRC cover revision through payload. CRC is CCITT-FALSE. Session and
message IDs are nonzero. Frames are 15..96 bytes; payloads are at most 81 bytes.
The function byte is a stable ID `0..254`; `255` selects board scope. This is an
explicit limit of 255 functions, not an eight-entry board array. Larger ID spaces
require a future envelope revision. No allocation, fragmentation, streaming, or
bulk transfer is implemented in this revision. Oversized/partial/bad-CRC/wrong-
revision frames are discarded before dispatch and do not consume an ID. A
transport must reject or boundedly reassemble data before passing one complete
frame; unbounded buffers and automatic version downgrade are prohibited.

Every response echoes session, message, command and function. Its payload starts
with `[status:u8][execution:u8]`, followed by command/profile-specific data.
The common prefix leaves at most 79 bytes of response data; a PROFILE request's
five-byte selector leaves at most 76 bytes of device payload. A response is never
identified by inspecting a sensor value's first byte.

| Execution | Meaning |
| --- | --- |
| REJECTED (0) | Operation did not execute and was not queued. |
| ACCEPTED (1) | Bounded asynchronous action accepted; query its token before assuming completion. |
| COMPLETE (2) | Firmware action finished; says nothing by itself about physical motion or safety. |
| UNKNOWN (3) | Execution may have occurred; reconciliation required. |

`OK` (1) requires ACCEPTED or COMPLETE. `ERROR` (2) requires COMPLETE and means a
confirmed operation failure, potentially with partial physical effects; query
state before deciding recovery. `OUTCOME_UNKNOWN` (12) requires UNKNOWN and no
extra data. All remaining statuses require REJECTED:

| Status | Meaning / retry policy |
| --- | --- |
| UNSUPPORTED (3) | Operation/core command not implemented or not advertised. Fix the request. |
| INVALID_CHANNEL (4) | Function does not exist, or command has the wrong board/function scope. |
| INVALID_PAYLOAD (5) | Malformed request or invalid values. No partial configuration/action. |
| UNAVAILABLE (6) | Device cannot operate, including safety dependencies. GET_STATUS gives the reason/recovery. |
| NOT_READY (7) | No usable acquisition is available yet; service conversion outside dispatch and issue a new read later. |
| BUSY (8) | Queue/resource capacity or active-configuration guard rejected admission. A later new-ID request is permitted. |
| RESOURCE_CONFLICT (9) | Configuration rejected; detail is `[owner:u8][kind:u8][resource:u16]`. Existing configuration is preserved. |
| PROFILE_MISMATCH (10) | Wrong profile ID/major or unsupported minor version. |
| STALE_DISCOVERY (11) | Generation changed; restart enumeration before enabling anything. |

Unrecognized status/execution values or invalid combinations are malformed
responses and cannot prove success/failure. Handler API failures/malformed replies
are converted to a cached OUTCOME_UNKNOWN, because the endpoint cannot know
whether the handler produced a side effect before failing.

## Core commands and discovery

Core operations are mandatory in revision 4. Board-only commands use function
255; PROFILE and DESCRIBE_FUNCTION require a real function ID. GET_STATUS accepts
either. Unsupported optional safety scope returns UNSUPPORTED. Retired global
device opcodes `02..06,09..0a` return UNSUPPORTED; device operations use PROFILE.
Lengths below exclude the common response prefix.

| ID / command | Request | Successful response data |
| --- | --- | --- |
| 01 IDENTIFY | empty | 52-byte board/limit description below |
| 07 ESTOP | safety domain:u16 (`0` = entire board) | empty, ACCEPTED or COMPLETE; query status |
| 08 RESET_SESSION | empty, new nonzero session | empty, COMPLETE; outputs disabled/configuration invalidated, no physical verification implied |
| 0b PROFILE | profile:u16, major:u8, minor:u8, operation:u8, profile payload | profile response; empty allowed for mutations |
| 0c DISCOVER | generation:u32, cursor:u16 | generation:u32, total:u16, next:u16, count:u8, up to four 13-byte descriptors |
| 0d DESCRIBE_FUNCTION | empty | 13-byte descriptor followed by metadata defined by its profile version |
| 0e GET_STATUS | empty | 19-byte board/function state below |
| 0f GET_DEPENDENCIES | generation:u32, cursor:u16 | generation:u32, next:u16, count:u8, up to sixteen dependency records |
| 10 RECOVER | safety domain:u16 | empty, ACCEPTED or COMPLETE; never enables actuators |

IDENTIFY data, in order:

```text
vendor:u32, product:u32, firmware_name:16 bytes,
boot_id:u32, discovery_generation:u32, function_count:u16,
max_frame:u16, max_inflight:u8, max_page_entries:u8,
max_handler_us:u32, safety_flags:u32,
protocol_min:u8, protocol_max:u8, supported_planes:u32
```

Vendor/product are informational. Revision 4 requires frame limit 96,
max_inflight 1, page limit 1..4, nonzero dispatch bound, boot ID and generation.
Supported planes are MESSAGE (bit 0, mandatory) and CYCLIC (bit 1, optional).
Unknown safety/plane bits are preserved. Enabling a required function requires
host support for its safety semantics, protocol revision, transport plane,
profile ID/major/minor and operations. The host must check advertised protocol
range contains the actual frame revision. EtherCAT also gates layout/slot sizes
using its compatibility descriptor before publishing frames. A serial probe
uses an explicit configured revision and bounded RESET/IDENTIFY exchanges;
failure reports incompatibility/unreachability, never a silent fallback.

Function descriptor:

```text
function:u8, device:u16, profile:u16, major:u8, minor:u8,
operations:u32, safety_domain:u16
```

Function IDs are unique, stable across reset/reboot and independent of
configuration order. Device IDs group functions of one physical device. A smart
actuator can expose four function IDs for motor control, encoder, temperature,
and diagnostics with the same device ID and independent profiles/operation
bitmaps. Profile ID/major zero are invalid; unknown nonzero IDs, operation bits,
and device IDs survive discovery even if the host cannot use them. Bit N
advertises operation N (0..31). New major versions may break a schema; minor
versions must retain old request/response layouts for explicitly requested older
minors. A host requests only a version it knows, not the advertised newest minor.

First DISCOVER uses generation=0,cursor=0. The returned generation is required on
subsequent pages; the cursor is an enumeration index, never a function ID.
`next=ffff` terminates. Zero functions returns an empty terminal page. Subsequent
cursors advance strictly by count; enumeration is sorted by function ID and
stable within a generation. The host rejects repeated IDs/cursors, mismatched
totals, incomplete catalogs and changing generations, including changes between
DISCOVER and DESCRIBE_FUNCTION (bracket metadata queries with IDENTIFY). Both
catalog and dependencies use the same generation. GET_DEPENDENCIES starts at
cursor=0 with the identified generation. Dependency records are
`[cause_domain:u16][affected_function:u8][effects:u8]`; multiple domains may affect
one function. Effects are output disable, power loss, execution interruption and
communication interruption (bits 0..3). Domain 0 covers the entire board; named
domains enumerate every directly/transitively affected function. A board-wide
communication interruption must be advertised in safety_flags as well.

The dependency index supports at most 65,535 records; a board must reject a
catalog/configuration that exceeds that bound rather than truncate dependencies.
Hosts bound enumeration to the cursor space (at most 4,096 dependency pages) and
reject repeated or non-advancing cursors. max_page_entries refers to DISCOVER;
GET_DEPENDENCIES has the fixed sixteen-record bound in this revision.

Discovery pages are at most 61 bytes and dependency pages 71 bytes before the
reply prefix, so board size never grows a response beyond the frame bound.
Metadata that cannot fit must use a separately versioned, bounded profile
operation; this revision does not imply a generic bulk-transfer service.

## One session owner per MCU, across all functions and transports

Firmware has exactly one `jw_endpoint_t`/`jw_firmware_session_t` per MCU. Serial,
mailbox and cyclic adapters enqueue into the same serialized dispatch loop.
The host resolves all motor/sensor/I/O handles and transport planes of that MCU
to **one coordinator**, including reconnect and teardown. Independently locked
per-driver or per-transport sessions are invalid. Two host owners cannot operate
the same MCU concurrently; an intentional takeover resets/disables the board.
Session IDs are correlation, not authentication or protection against resets.

The coordinator allocates monotonically increasing message IDs **when dispatching**,
not when producers enqueue work. It holds one in-flight slot across every plane
until a correlated reply or a deliberate resolution of an uncertain outcome.
It never holds transport/master locks while waiting. Before wrap, stop admission,
disable outputs, reset with a fresh session, and reconcile/configure explicitly.
Firmware admits IDs greater than its previous ID; gaps are legal. Lower IDs and
changed requests reusing an ID are discarded. An exact duplicate of the most
recent request replays the identical cached response without calling handlers,
including ACCEPTED, errors, and OUTCOME_UNKNOWN. No earlier replay cache exists.

A timeout/cancellation after publication is a **host-local unknown outcome**,
not a confirmed firmware rejection. Before sending another ordinary request,
retry the exact same complete bytes/ID within a bounded retry budget. Retrying
with a new ID can execute twice and is prohibited for uncertain mutations. A
NOT_READY/BUSY reply is known rejection: later attempts get new IDs, otherwise
exact retries would replay the same rejection forever. Accepted asynchronous
work must not be resubmitted: query status/data using subsequent IDs and the
original action token. Backends retain observable completion until the next
state-changing operation on that function; the coordinator gates such mutations
until completion is reconciled. Background sensor acquisition is not repeated
by request replay and is canceled/invalidated on reset as appropriate.

When retry budget expires, retire the unresolved request, mark affected state
unknown and gate further mutations; send only state queries, a bounded stop, or
reset/recovery. Queries can show actual resulting state but cannot necessarily
prove whether a non-idempotent historical action executed. Never automatically
resubmit such an action across a new session/reboot. Operation-specific durable
transaction IDs, if needed, belong to a future profile; volatile replay does not
provide exactly-once execution across reboot.

A pending slow request cannot monopolize ESTOP: the shared coordinator can retire
its ID and send a higher-ID stop through a reserved safety slot. This is still
one published request at a time. The old response/action outcome is then unknown;
never retry its retired ID. The host queries all affected states afterward.
Safety cannot preempt an already executing synchronous callback, hence the hard
handler bound. Out-of-band watchdog/safety behavior belongs to board firmware.

Reconnect blocks all users of every plane, cancels/invalidates retained slots,
flushes streams, and RESETs once with a fresh session. Reset disables outputs,
invalidates configuration and pending work, and **does not clear an ESTOP latch**.
IDENTIFY, complete discovery/dependency/metadata validation and board/all affected
function status queries reconcile actual state before desired protobuf settings
are reapplied. Actuator re-enable requires an explicit authorized enable action.
A reboot changes boot_id and clears session/replay; stale samples/clock mappings
are invalid. Old normal frames cannot execute in the new session. Delayed resets
from another session can still disable the board; adapters must retire old reset
attempts as well, and the host must treat a resulting lost session as unreachable.

## Bounded acquisition, scheduling and configuration

Every handler and reset callback must return within IDENTIFY.max_handler_us, the
board's tested worst-case dispatch budget. The endpoint invokes synchronous
callbacks; it cannot enforce or preempt that budget. A backend must not advertise
a bound it cannot honor. Hosts reject a dispatch bound that exceeds their configured control/stop budget.
No conversion, serial-bus timeout, blocking allocation,
or unbounded loop runs in dispatch. Reads return cached samples or NOT_READY;
conversion/bus work uses a cooperative state machine or bounded background worker
that cannot starve dispatch/control/watchdogs. A queued configuration/stop returns
ACCEPTED with the session/message action token, followed by queryable completion.
Partial apply failures return ERROR and observable fault/recovery state. Queue
capacity rejection returns BUSY before any side effect.

Host and firmware queues are bounded with safety capacity reserved independently
of sensor work. Priority is stop/watchdog, deadline-bound control, configuration,
then sensor polling. A declared polling quota prevents starvation when control
has slack; sensor work cannot extend a control deadline. Coalesce unissued reads
or targets only when profile semantics explicitly permit it; never coalesce a
published/uncertain mutation or relative/non-idempotent action. Producers see
admission failure instead of unbounded latency. Sensor deadlines include
conversion separately from the request's dispatch/transport deadline. EtherCAT
mailbox budgets remain below cyclic slack as defined in the companion plan.

The firmware owns one board-wide resource registry for pins, timer groups, ADC
channels/units, buses, DMA/interrupt resources and power dependencies. A profile
configuration specifies intended settings from the host protobuf; the backend
translates them into claims and validates the **whole proposal** before applying
anything. Claims are keyed by kind/resource ID and function owner. Sharing is
allowed only if every claimant opts in and compatibility settings agree (e.g.
PWM timer frequency, ADC mode or bus timing). Resource kinds/settings can be
extended by a backend without changing core framing. A sensor cannot claim an
active STEP pin, and a timer change cannot disrupt another PWM function.

Configuration is serialized board-wide and allowed only in a permitted disabled
state. Ownership changes are staged with hardware changes, committed together,
and rollback restores the previous configuration on rejection/apply failure.
If rollback cannot be guaranteed, latch all potentially affected outputs
disabled and expose ERROR/fault/reconfiguration status; never claim unchanged
hardware. Explicit disabled release or session reset releases claims; ESTOP can
retain claims until recovery. The `jw_resource_replace` helper provides bounded,
all-or-nothing registry preflight/commit; it does **not** provide GPIO rollback
or check enabled state. Backends must stage hardware before committing the
registry, or snapshot/rollback under the same lock. No callback may reconfigure
another owner's resource implicitly. Runtime discovery never invents wiring.

## Safety state and recovery

IDENTIFY.safety_flags advertises board ESTOP, named-domain ESTOP, physical
verification support and communication interruption (bits 0..3). The dependency
catalog declares what an ESTOP can affect. GET_STATUS is authoritative after lost
notifications/reconnect; events are not required. Its data is:

```text
state:u8, reason:u8, recovery:u8, stop_phase:u8, verification:u8,
epoch:u32, action_session:u32, action_message:u32,
action_execution:u8, action_status:u8
```

State is UNKNOWN, DISABLED, READY, ACTIVE, UNAVAILABLE or FAULT (0..5). Reasons
are NONE, SESSION_RESET, ESTOP, SHUTDOWN_DEPENDENCY, DEVICE_FAULT or REBOOT (0..5).
A temperature function stopped by shared power reports UNAVAILABLE /
SHUTDOWN_DEPENDENCY, and its domain maps it to the board ESTOP; it does not report
a sensor conversion fault. epoch changes on every observable transition (including
stop completion) within boot_id. At epoch/generation exhaustion, reboot to a new
boot ID before reuse. Action IDs identify accepted/latest state-changing work;
execution/status allow polling completion or confirmed failure. Zero IDs use
status INVALID and execution REJECTED to mean no retained action. One pending
mutation per function is sufficient for this revision; stop can cancel it and
replace the token, leaving the canceled historical outcome unknown.

stop_phase independently reports NONE, ACCEPTED, ACTION_COMPLETE (0..2).
verification reports UNSUPPORTED, UNKNOWN, VERIFIED, NOT_VERIFIED (0..3).
ESTOP acknowledgment, firmware output-disable completion, and physical verification
are different facts. A board can report ACTION_COMPLETE with UNSUPPORTED physical
verification (e.g. STEP/DIR without an independent stop sensor). VERIFIED is only
permitted when advertised independent evidence meets the profile's physical safe-
state definition; invalid/stale evidence becomes UNKNOWN or NOT_VERIFIED. An
aggregate board verification cannot exceed the least-confirmed affected actuator;
sensors/power effects are reported separately. Neither an OK reply nor transport
acceptance is a physical-safety assertion.

Recovery flags are RECONFIGURE, EXPLICIT_ENABLE and CLEAR_ESTOP (bits 0..2).
RECOVER clears the selected latch only after stop completion and board interlocks
permit it; it restores sensor availability independently and never enables an
actuator. Reset/recovery cannot implicitly restore actuator targets/enable. The
host must apply intended configuration if required, query completion and explicitly
enable. A sensor returning data is not permission to enable anything else.

If ESTOP interrupts communication, the host marks board/affected functions
UNKNOWN/unreachable; it cannot report stop success from an acknowledgment that
never arrived or a disconnected link. On reconnection it queries actual states
and recovery requirements before operations resume. Firmware retains safety
latches through a session reset; reboot must start with outputs disabled and
report its own actual startup/latch/interlock policy.

## Initial profiles and data contracts

Profile registry: IDs `1..7fff` are repository-coordinated, `8000..ffff` are vendor
extensions identified with vendor/product. IDs are never reused. Function identity
and profile ID/version, not a global channel kind, select a codec. These initial
profiles are **major 1, minor 0**; advertising reserved operations is prohibited.
Other typed sensors (integer/vector/binary/etc.) use separate versioned profiles.

| Profile | Operations | Metadata from DESCRIBE_FUNCTION | Successful data |
| --- | --- | --- | --- |
| 1 actuator | CONFIGURE=0, SET_TARGET=1, GET_FEEDBACK=2, ENABLE=3, DISABLE=4 | drive:u8, target_mode_bits:u32, position_unit:u16, velocity_unit:u16, effort_unit:u16, feedback_clock:u8, max_feedback_age_us:u32 (16 bytes) | target/config/enable/disable: empty; feedback: measurement metadata + position:f32, velocity:f32, fault_flags:u16 (28 bytes) |
| 2 scalar sensor | READ=0 (empty) | type:u8 (=SCALAR_F32), unit:u16, clock:u8, max_age_us:u32 (8 bytes) | measurement metadata + value:f32 (22 bytes) |
| 3 I/O | READ=0 (empty), WRITE=1 | interface:u8 (=DIGITAL or ANALOG_F32), unit:u16, clock:u8, max_age_us:u32 (8 bytes; digital uses dimensionless 0/1, analog uses f32) | read: measurement metadata + value (19/22 bytes); write: empty |

Actuator CONFIGURE v1.0 is implemented only for STEP_DIR and uses the existing
11-byte host pin/settings schema: max_pulse_rate:u32, invert_dir:u8,
enable_active_low:u8, step_pin:u8, dir_pin:u8, enable_pin:u8, pulse_width_us:u16.
Non-STEP_DIR backends must not advertise CONFIGURE until a profile version/schema
for it is defined. ENABLE/DISABLE take empty payloads. SET_TARGET mode 1/2/3 is
`mode:u8,value:f32`; COMPLEX=4 is `mode,position:f32,velocity:f32,torque:f32`.
Only advertised modes are accepted; all fields are finite and validated before
apply. Combined fields have no omission semantics or multi-device atomicity.
Feedback fault_flags are profile-defined device diagnostics; unavailable state
and safety dependency reasons belong in GET_STATUS. Target acceptance means the
firmware setpoint update completed, not that physical motion reached that target.

Measurement metadata (18 bytes):

```text
validity:u8, clock:u8, boot_id:u32, acquisition_time_us:u64, sequence:u32
```

Validity is VALID, STALE or INVALID (1..3). Values are not usable when invalid;
NaN is only permitted for invalid floating samples. Sequence counts acquisitions,
not queries; cached reads and request replay preserve sequence/timestamp. Sequence
wrap uses modulo-u32 comparison only within a boot and a bounded observation gap;
otherwise the host invalidates continuity. Clock 1 is unsigned microseconds since
boot; clock 2 is a separately synchronized microsecond domain and requires a
profile/board synchronization contract before advertising. Both have boot-scoped
timestamp mappings; reset alone does not restart the clock, reboot does. A sample
boot ID must equal IDENTIFY.boot_id. Host UTC/ROS timestamps are not assumed to
share either clock. max_age_us (max_feedback_age_us for actuators) defines the maximum usable sample age;
firmware reports STALE when exceeded. Host correlation/synchronization or local
receipt age must also enforce freshness; repeated cached replies cannot refresh
age. Incomplete/failed conversions cannot manufacture fresh values. Scalar sensor
units include dimensionless, radian, radian/sec, Celsius, volt, ampere and metre;
NATIVE requires documented vendor scaling. Unknown required units/clocks block use.

I/O write payloads are exactly a 0/1 byte or f32 in declared units. Freshness belongs
to READ replies, not write payloads. This PR provides the sensor, actuator and I/O
value/sample codecs; boards implementing I/O must serialize its full metadata
contract. Additional schemas do not change session/framing.

Events/streaming are intentionally unimplemented and unadvertised. A future
extension must define a distinct message kind/correlation namespace, subscription
lifetime, stream IDs/sequences, rate/backpressure, loss/reboot/freshness and
reconciliation semantics. An unsolicited sample never satisfies a request, and
notifications never replace GET_STATUS. Bulk transfer similarly needs bounded
chunking/ownership/timeouts before advertising support.

## Stacked implementation and integration gates

| Follow-up PR | Concrete implementation | Required hardware-free integration tests before migration |
| --- | --- | --- |
| H — shared host board engine/scheduler | MCU-identity cache shared by actuator/perception/I/O handles; one lock/queue/ID allocator; bounded admission, priority/deadline/fair polling; retry/unknown outcomes; complete compatibility and reconnect reconciliation | Concurrent motor and sensor callers on one MCU; shared serial/mailbox/PDO ordering; exhausted queues; lost replies/late responses; stop preemption; profile/board compatibility; one reset across reconnect; no implicit actuator enable |
| F — mixed firmware backend migration | Catalog/status/dependency handlers, bounded acquisition workers, shared resource registry with hardware staging/rollback, safety latches/watchdogs and stop evidence | Slow/failing conversions/bus timeouts do not exceed advertised dispatch/stop budget; pin/timer/ADC/bus conflicts and rollback failure; async completion polling; sensor power shutdown reasons; unsupported vs independently verified stop; reboot/reset cancels work and disables outputs |
| T — serial/EtherCAT adapter migration | Complete frame handling, serial flush/reconnect, one EtherCAT master owner, bounded mailbox budget and shared endpoint/PDO publication arbitration; revision-4 compatibility descriptor/ESI artifacts | Retained/mismatched frames rejected; management only mailbox; only approved actuator profile operations cyclic; mixed-plane contention; publication/cancellation/timeout distinction; lost ESTOP communication gives unknown; reconnection queries all affected state before resuming |

The existing board/comm separation plan describes migration order; H/F/T are
explicit responsibility slices of those stacked changes, not implementations
claimed by this PR. No hardware migration may land before its corresponding
integration gate. New device profiles and optional streaming/bulk services may
follow independently.

This PR's `joshua_wire_mixed_device_test` executes the real codec/session/endpoint
against a twelve-function fake MCU and shared fake host/transport coordinator.
It covers multifunction/unknown-profile discovery, generation invalidation,
interleaved actuator/sensor traffic, slow/failing/cached/stale acquisition, dropped
response retries across two transport paths, unknown side effects, pin/PWM timer
conflicts, stop acknowledgment/completion, dependency state, recovery, reboot,
reconnect and bounded scheduling admission. The scheduling/acquisition portions
are fixtures, not evidence of production thread/deadline guarantees; H/F/T must
supply those integration tests. Golden core/profile payload and frame tests
cover exact byte layouts, CRC, limits, malformed values, IDs and JW1 preservation.
