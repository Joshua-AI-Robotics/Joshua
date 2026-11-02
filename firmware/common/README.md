# Shared firmware and JoshuaWire

JoshuaWire **0.2.0 / wire revision 4** connects the host board engine and MCU
firmware. The normative [protocol contract](../../docs/JOSHUA_WIRE.md) defines
mixed-device discovery, versioned profiles, session ownership, reply semantics,
safety state and the H/F/T integration gates. Earlier revisions are incompatible;
JW1 and the legacy host paths are retired in the integration stage.

## Contents

- `joshua_wire.{h,c}`: bounded 15..96-byte complete frames, CRC and correlation.
- `joshua_wire_commands.{h,c}`: core commands, opaque function descriptors,
  paged discovery/dependencies, common status/execution replies and state codecs.
- `joshua_wire_profiles.{h,c}` and actuator/sensor/I/O headers: profile v1.0
  metadata and payload codecs. `jw_drive_t` is actuator metadata only.
- `joshua_wire_resources.{h,c}`: atomic resource-ownership preflight/commit;
  backends own hardware application and any necessary rollback.
- `joshua_wire_firmware_session.{h,c}` and `joshua_wire_endpoint.{h,c}`:
  one MCU session, exact duplicate replay and complete-frame dispatch without I/O.
- `joshua_device_runtime.{h,c}`: bounded, allocation-free catalog, dependencies,
  status, safety and per-function profile dispatch. Callers serialize the runtime
  and supply stable IDs, metadata, boot identity and bounded backend callbacks.
- `joshua_actuator_handler.{h,c}`: portable C actuator-profile execution shared
  by AM243, Teensy and ESP32. Decodes typed requests, validates advertised modes
  and common admission guards, encodes replies and records action outcomes.
- `joshua_stepdir_device.{h,cpp}` / `backend_stepdir.{h,cpp}`: the shared
  Teensy/ESP32 actuator backend. GPIO claims are checked before configuration;
  enabled reconfiguration is BUSY. Feedback is a main-loop acquisition of counted
  pulses/commanded values, not encoder measurement or physical-stop evidence.
- Teensy/ESP32 use platform hardware entropy for boot identities; no persistent
  boot-counter storage is reserved.
- `frame_transport.h`, serial assembler/transport sources: bounded complete-frame
  I/O and retained whole responses. Partial input or blocked transmission does
  not repeat dispatch. Motor service continues between iterations.
- `joshua_wire_ethercat.h`: layout-2 constants, 108-byte mailbox envelopes and
  112-byte PDO images. `joshua_ethercat_profile.{h,c}` implements shared CoE/PDO
  publication, retained replies, session state and function-ID watchdog slots.

## Actuator backend contract

The shared actuator handler registers through `joshua_device_runtime`; sensor
and I/O handlers register independently. Actuator execution stays outside the
core codec. AM243 supplies `am243_software_backend`; Teensy/ESP32 supply the
STEP/DIR callbacks in `joshua_stepdir_device`.

Backend callbacks own device limits, resource staging/rollback, bounded acquisition
and physical actions. They return explicit status/execution results. Rejected work
has no effects; accepted, failed or uncertain work retains backend-authored state
and queryable action IDs. Only completed successful mutations get normal state
reporting from a fresh backend snapshot. Successful DISABLE preserves fault and
ESTOP recovery state. Reset/ESTOP/recovery remain board-wide runtime safety callbacks.
This consolidation is part of F; production scheduling, rollback and hardware
qualification gates in H/F/T remain outstanding.

## Runtime behavior

IDENTIFY advertises protocol/planes/limits, boot identity and safety semantics.
DISCOVER pages stable function descriptors; DESCRIBE_FUNCTION supplies versioned
metadata. Vendor/product IDs are informational. Actuator, sensor and I/O schemas
are separate from core framing. Unknown optional profiles remain discoverable.
Host configuration remains the intended wiring/settings source of truth.

Every response starts with status/execution. Handler failure is cached as
OUTCOME_UNKNOWN; exact retries cannot repeat side effects. RESET disables outputs
and invalidates configuration while preserving ESTOP/fault latches. RECOVER is
explicit and never enables. GET_STATUS exposes firmware action completion,
recovery requirements and unsupported physical verification separately.

The [host engine](../../robot/board/joshua_wire/README.md) checks the complete
catalog, dependencies and metadata before applying desired actuator settings.
One session serializes message and cyclic operations. Paired EtherCAT admits only
actuator v1.0 target/feedback to PDO; discovery, status and other profiles stay on
CoE. The AM243 [TI adapter](../am243/joshua_dual_transport/README.md) maps 28 words
per PDO and has a software-only channel. Its UART and EtherCAT artifacts are
separate owners; simultaneous transports on one MCU still need arbitration.

## Serial transmit fault budget

Teensy, ESP32 and AM243 UART install a local-fault callback on the shared serial
adapter. A response must leave its adapter-owned TX slot within 20 ms. The
deadline is checked on every receive poll and send attempt, even when the
application holds a second response and cannot admit another request. Partial
writes do not extend the deadline. At 115200 baud a maximum 96-byte frame takes
about 8.4 ms on the wire; the 20 ms budget allows normal frame draining while
bounding a blocked connection. Expiry invokes the stop callback on the next
serial service iteration (AM243 normally services every 1 ms); it is not a
hardware watchdog against a stalled firmware loop.

The callback disables and unconfigures every actuator, latches the runtime's
stop interlock, and exposes `DEVICE_FAULT` with `ACTION_COMPLETE` and unsupported
physical verification through `GET_STATUS`. It has no host action token. A queued
ESTOP can remain unread until TX resumes, but outputs stop independently of it.
Both response slots and the exact-retry cache retain their original bytes and
ownership. A cached successful ENABLE reply therefore records the earlier action,
not the current safety state; hosts must reconcile through `GET_STATUS`.
Session reset preserves the latch. Recovery requires `RECOVER`, reconfiguration
and explicit enable. These tests establish simulated output disabling, not a
physical stop-time guarantee.

## Porting JW EtherCAT to another board

Supply stable function/profile metadata and bounded command/reset/stop/enabled
callbacks to the portable profile. Serialize its calls, service acquisition and
watchdogs independently, and match SII/ESI and object/PDO mappings to layout 2.
Reset must disable outputs and preserve safety latches; recovery never enables.
Vendor/product IDs do not register a new core board type. A physical backend
must validate resources and stage or roll back hardware configuration.

## Validation and remaining gates

Use Compose `test-u22`/`test-u24` from [CONTRIBUTING](../../CONTRIBUTING.md#testing-and-ci).
Native tests cover golden core/profile bytes, a twelve-function mixed-device
fixture, real host/firmware endpoint integration, stable IDs, shared IDs across
planes, unknown outcomes, ESTOP/reset/recovery, resource conflicts, cached sample
metadata and controlled-clock EtherCAT watchdogs. Arduino/TI fakes open no real
hardware; PlatformIO and AM243 Makefiles build the same production sources.

The full priority/deadline scheduler, stop preemption, generic host sensor/I/O
bindings, physical-backend rollback and hardware timing/boot/safety qualification
remain gates in the normative contract. Serialized loopback tests do not establish
those guarantees. No firmware is flashed automatically. See the
[manual validation guide](../../docs/JOSHUA_WIRE_VALIDATION.md) for explicit
hardware procedures and `--allow_recover` when recovering a latched stop.

The [SOES binding](soes/README.md) supplies a separately pinned slave stack,
fixed object dictionary and PDO packing. It reuses the same revision-4 profile
and remains subject to the same AM243 hardware qualification gates.
