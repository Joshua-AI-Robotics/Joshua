# LP-AM243 — JoshuaWire 0.2.0 serial and EtherCAT firmware

One AM243 image that keeps TI's EtherCAT simple demo active while also serving
JoshuaWire (JW) `0.2.0` on UART0 at 115200 baud. The build layers Joshua-owned source
and a small patch over the externally installed TI Industrial Communications
SDK; it does not modify or vendor the SDK.

The default image serves JW UART alongside TI's EtherCAT echo demo. Its
serial channel reports `STEP_DIR` and implements command/response handling
in software; it does not drive STEP/DIR GPIOs or move motors.
Its dedicated task composes the shared JW endpoint with the nonblocking
[`transport_uart`](src/transport_uart.h) adapter. The adapter exclusively polls
UART0's 64-byte FIFO and uses the common incremental serial assembler. It copies
a complete response into a retained transmit slot, then drains up to 64 bytes
whenever the FIFO is empty. The task retains additional blocked responses without
repeating a command and yields between iterations. Binary
UART ownership starts after SDK startup logs are disabled; no SDK UART
transactions or other writers may share that FIFO.

`src/am243_software_backend.{h,c}` registers the software channel with the shared
`joshua_device_runtime` and `joshua_actuator_handler`. UART and JW EtherCAT reuse
the same actuator handling as the Teensy/ESP32 STEP/DIR backend. This file owns
software state, cached feedback and safety actions; payload decoding, common
guards and reply encoding live in `firmware/common/`.

## Prerequisites

- TI Industrial Communications SDK `09.00.00.03`
- CCS/ARM Clang, PRU CGT, and SysConfig versions listed in the adjacent
  `ti_ethercat_simple_demo_v1/README.md`
- The SDK configured by the existing AM243 setup scripts

## Install

Follow the TI guide for SDK/toolchain setup. The build script copies SDK inputs
into a temporary tree, patches the UART task entry point, and enables RNG/SA2UL
in that image's SysConfig for a fresh hardware boot identity. It does not modify
or vendor SDK sources. The generated firewall configuration grants the R5 core
SA2UL MMR access, matching TI's AM243 RNG example; crypto configuration alone
does not permit the TRNG engine-enable register access.

## Build

```bash
firmware/am243/joshua_dual_transport/scripts/build.sh
```

Artifacts are written under the ignored `out/` directory. The build uses a
temporary working tree and leaves TI SDK sources unchanged.

The default UART outputs are `am243_dual_transport_rev4.release.*`. The script
forwards arguments to `make`, allowing compiler/tool path overrides. EtherCAT
in this image remains TI's echo demo; use the separate JW profile below for
Joshua EtherCAT runtime. See [JW limits](../../README.md#joshuawire-serial)
and the [serial validation procedure](../../../docs/JOSHUA_WIRE_VALIDATION.md).

### Opt-in JW EtherCAT profile

The same overlay also builds a separate **EtherCAT-only** JoshuaWire artifact:

```bash
JOSHUA_ETHERCAT_PROFILE=jw \
JOSHUA_COMM_WATCHDOG_US=500000 JOSHUA_TARGET_WATCHDOG_US=250000 \
firmware/am243/joshua_dual_transport/scripts/build.sh
```

Those intervals are software-demo examples, not validated motor-safety limits.
Both must be explicit (10000–999999999 microseconds); there are no watchdog
defaults. Outputs are `out/am243_ethercat_jw.release.*`. The default profile
remains `ti-demo`, building JW UART alongside TI's echo PDOs.
The JW profile starts no UART protocol task; the console remains available for
SDK logs. It has one software-only channel and **no STEP/DIR GPIO backend**.

Source responsibilities:

- `src/am243_software_backend.{h,c}`: transport-neutral software channel callbacks
  registered with the common actuator handler, shared with the UART artifact.
- [`../../common/joshua_ethercat_profile.h`](../../common/joshua_ethercat_profile.h)
  and `.c`: shared board/stack-independent object/PDO protocol, one session
  across CoE and PDO, retained replies, and per-channel freshness watchdogs.
- `src/joshua_ethercat_ti.{h,c}`: SDK object registration, complete image copies,
  callback serialization and an independent RTOS watchdog task. Supplies AM243
  identity and software-channel callbacks to the shared profile; a different
  board supplies its own stack/drive adapter, not a new host transport.
- `patches/jwec_profile.patch`: replaces TI's demo object/PDO setup and dispatch
  in a temporary SDK source copy. TI demo EEPROM-persistence callbacks are not
  installed for this profile; the SDK builds the profile from its new mapping.

The descriptor reports `am243-ec-jw`, JW/layout-v2, 112-byte input/output images
and transport bits `6` (CoE + PDO). The matching fixed mapping is:

| Direction | PDO | Mapped entries | Size |
| --- | --- | --- | --- |
| Master → slave | `0x1600` | `0x7000:01` through `:1c` (hex), 28 × UINT32 | 112 bytes |
| Slave → master | `0x1a00` | `0x6000:01` through `:1c` (hex), 28 × UINT32 | 112 bytes |

Assignments `0x1c12`/`0x1c13` each contain the corresponding single fixed PDO.
The image arrays are PDO-only; CoE accesses cannot modify their contents.
All management objects use subindex zero, without Complete Access:
`0x2000` descriptor (36 bytes, read), `0x2001` session/reset (8, read/write),
`0x2010` request (108, write), `0x2011` retained response (108, read), and
`0x2012` exact-generation acknowledgment (4, write). Byte layouts are defined
in [the shared contract](../../common/joshua_wire_ethercat.h).

The evaluation identity inherits TI's SDK vendor setting; real SII discovery
reported `0xe000059d`, with Joshua profile product `0x4a570002` and revision
`0x00020001`. This is not a registered
commercial product identity. Resolve identity/redistribution requirements
before public hardware distribution. No standalone ESI XML is provided yet;
the mapping above documents the profile, and a cached TI-demo ESI is incompatible.

All AM243 artifacts use the shared `src/boot_identity.c` hardware RNG path for a
nonzero boot ID, preserved across session resets. The EtherCAT artifacts reject
startup before registering objects or watchdog tasks when RNG acquisition fails
or returns zero identity data; boot-relative uptime is never an identity source.

Only a **new valid command**, not repeated reads of an unchanged PDO output
image, refreshes the command-progress watchdog. ENABLE starts the target grace
period; successful SET_TARGET refreshes target freshness. GET_FEEDBACK can feed
command progress but cannot keep an old target alive. An enabled host must issue
fresh targets within both configured limits. This is deliberately stricter than
a packet-arrival watchdog: the SDK application loop alone does not establish
that a new EtherCAT output packet arrived.

The independent task checks time every 1 ms plus RTOS scheduling delay. Expiry,
loss of OP or protocol faults disable the software channel, clear its target
and latch ESTOP. Feedback fault bits are `0x1` command-progress timeout, `0x2`
stale target, `0x4` state/clock fault and `0x8` malformed PDO. Reset to a
different nonzero session preserves latches. Explicit RECOVER clears them while
outputs stay disabled; repeating the current reset is a no-op.
An all-zero output is an explicit stop; generation zero with the active session
only cancels the command slot. Replies remain until the matching acknowledgment;
duplicates never execute again or refresh watchdogs.

Native tests exercise the production core with a controlled clock and through
the host's paired CoE/PDO adapters. Production Ethernet timing requires hardware
qualification. Software watchdog behavior does not establish physical motor
safety, CPU-halt coverage or hard-real-time timing.

### Boot identity bench record — 2026-10-26 UTC

An authorized LP-AM243 run with motor power off flashed the JW TI-stack image
built with communication/target watchdogs of 20000/10000 us. Its signed appimage
SHA-256 was `47e96a9572618330621e3612e69cc2f286c954e7309ced8479695fc7da88e0ca`.
The first hardware attempt exposed a missing SA2UL firewall grant: the R5 took
a synchronous data abort while reading `0x40901000` in `SA2UL_engineEnable`.
Adding the SDK RNG example's grant to the isolated SysConfig overlay corrected
startup; both UART/TI-demo and JW artifacts cross-built afterward.

Over the direct `enp5s0` link, the corrected image advertised `am243-ec-jw`,
revision 4/layout 2, with 112-byte input/output PDOs. Two USB-C power cycles
produced boot IDs `0xc9d5fe7e` and `0xaf024889`. On each boot, five distinct
session resets and 15 correlated IDENTIFY replies preserved that boot's ID.
The probe used startup CoE SDOs only: it never requested OPERATIONAL or sent
actuator configuration, enable or target commands. This verifies RNG startup
and boot/session identity behavior on this board, not entropy quality,
EtherCAT timing, watchdog latency or physical motor safety.

### Callback ownership and stop priority

All `JoshuaChannel` access, including command/reset/stop callbacks, feedback
acquisition and enabled-state reads, belongs to one externally serialized owner.
The generic C callbacks borrow a non-null `JoshuaChannel*` through `void*`; its
storage outlives the endpoint/profile. The callbacks have no internal locks.
The UART artifact uses a single task. The TI JW EtherCAT artifact uses short
IRQ-disabled critical sections on one R5 core; FIQ handlers and other cores
must not enter that owner. UART and JW EtherCAT do not run together.

The TI stop-output callback latches a fault even when the channel is already
disabled. A stop epoch invalidates SDK snapshots admitted before a local stop,
watchdog fault or successful protocol ESTOP. Interrupted CoE callbacks return
unsupported access and stale PDO snapshots are discarded before dispatch. A
caller must reconcile status and request recovery explicitly. Reset alone
preserves the stop/fault latches and cannot restore configuration or enable.

The watchdog task runs at `TaskP_PRIORITY_HIGHEST`, above SDK 09's application
task (`HIGHEST-1`), and checks once per 1000-us sleep interval. SDK I/O and sleeps
remain outside the owner critical section, so a stalled vendor call cannot hold
the channel lock. A running critical section still delays the watchdog; its
maximum duration and actual scheduling/detection latency require R5 hardware
qualification. This is not a measured 1-ms stop guarantee or a physical stop.

`//firmware/am243/joshua_dual_transport:ethercat_ti_test` compiles the production
TI bridge as C against native SDK substitutes. It exercises stop/enable,
stop/recovery, stop/reset, watchdog activity during vendor I/O, stale PDO
snapshots, repeated faults and concurrent callbacks. The broader host priority
scheduler remains a separate H/F/T integration gate in `docs/JOSHUA_WIRE.md`.

### Opt-in SOES replacement

**Candidate; hardware qualification incomplete.**
Register receive deadlines remain affected by the
[master-side NIC timing issue](../../../robot/comm/ethercat/README.md#known-master-side-nic-timing-issue).
The TI profiles remain available until the replacement passes real-board
qualification and a continuous run beyond one hour. No proprietary timeout
is patched out.

```bash
JOSHUA_ETHERCAT_PROFILE=jw-soes \
JOSHUA_COMM_WATCHDOG_US=2000000 JOSHUA_TARGET_WATCHDOG_US=1000000 \
firmware/am243/joshua_dual_transport/scripts/build.sh
```

Run with the external SDK/toolchain available in the development container.
`curl` downloads the hash-verified [SOES revision](../../../third_party/soes/README.md).
The build uses SDK `09.00.00.03` hardware configuration, RTOS/driver libraries,
and ICSS FWHAL/PRU firmware, **not** TI's evaluation slave stack or Beckhoff SSC.
The SDK's SSC example supplies build/SysConfig rules and BSD-licensed board
initialization only; no SSC source or stack library is compiled/linked.

SOES links the same hardware RNG helper as the UART and TI EtherCAT artifacts.
Both build scripts append `syscfg/boot_identity.syscfg`, sharing the crypto/RNG
configuration and required SA2UL MMR firewall grant. Entropy failure prevents
protocol startup. Native RNG regressions cover repeated startup uptime,
session-reset stability and RNG failure; this SOES image still needs physical
boot-identity verification after a deliberate flash.

Outputs are `out/am243_ethercat_jw_soes.release.*`, including the link map.
The upstream SOES source archive and LICENSE accompany them; the source patch
is in `third_party/soes`. Existing TI artifacts are not overwritten.

Production source roles:

- [`../../common/soes/`](../../common/soes/README.md): shared SOES dictionary,
  JW CoE/PDO binding and stack settings; no AM243 dependencies.
- `src/joshua_ethercat_soes_am243.c`: AM243 boot, PRU register/mailbox/triple-buffer
  access, read-only RAM SII and independent watchdog task. Reuses the software
  channel and common actuator handler through `am243_software_backend`; no UART
  protocol or motor GPIOs. Polls at an initial 1 ms cadence,
  which is **not** a validated end-to-end cycle-time guarantee.
- `Makefile.soes`, `scripts/build_soes.sh`, `patches/soes_soc.patch`: isolated
  firmware build, pinned dependency download and removal of the unused SSC
  header from a temporary copy of TI's board initialization source.

The descriptor label is `am243-soes`; IDENTIFY uses the shared backend's
`am243-software` name. The layout and host transport are unchanged. Bench
SII/CoE identity is `0xe000059d` /
`0x4a570002` / revision `0x00020002`, **not a registered product identity**.
SII contains four fixed SMs and FMMU types; the master obtains PDO mapping
through CoE. No standalone ESI is provided. Physical EEPROM is neither loaded
nor written; runtime identity/mapping writes and segmented CoE writes are rejected.

#### SOES bench record — 2026-10-26

An authorized LP-AM243 run with motor power off flashed the corrected SOES
artifact, built with 5000000-us command-progress and 2000000-us target watchdogs.
Its signed appimage SHA-256 was
`151439ed3eab38bb68591366f2f1fed64c92c52a522e85955994b596815d6b55`.
The console reported SOES ready without the TI slave stack. Discovery on
`enp5s0` returned `Joshua AM243 JW SOES`, product `0x4a570002`, revision
`0x00020002`, `am243-soes` descriptor and 112-byte input/output PDOs.

Two USB-C cold boots produced distinct IDs `0x9c7cca7b` and `0x9986c086`.
Each remained stable across five session resets and 15 correlated IDENTIFY
replies. The production master/paired adapter probe then reached OP and verified
CoE configuration, PDO target/feedback, exact retry, ESTOP preservation across
session reset, target-watchdog expiry, rejected re-enable and explicit recovery.
This exercised the software channel only; it did not drive physical motor GPIOs.

The planned 65-minute endurance run did **not** complete. With a 50-ms cycle,
5-ms process/state budgets, 5-ms mailbox-step budget and 5-ms scheduling guard,
it reached 30 seconds / 150 checked target-feedback rounds at WKC `3/3` before
a state-register read at `0x0130` exceeded its budget (5071 us, WKC `-1`). A
second attempt widened only the state budget to 20 ms and stopped during setup
on a mailbox-register read at `0x0805` (5000-us budget, WKC `-1`). These register
timeouts remain a qualification blocker; the runs do not establish their root
cause or resolve the documented master-side NIC issue.

The console continued reporting uptime without a reboot or AL error. After
both failed attempts, a startup-CoE check confirmed the same second boot ID,
another five resets / 15 IDENTIFY replies, and retained unavailable/ESTOP status
with stop action complete and explicit recovery required. The host probe was
closed with the software channel stopped. Native preemption regressions cover
the precise interrupted-RECOVER race; this bench did not force that interleaving
on the R5 or qualify physical outputs, timing bounds or endurance.

#### TODO: Retire TI-stack profiles after SOES qualification

Keep `JOSHUA_ETHERCAT_PROFILE=jw` available as a comparison implementation
during SOES bring-up. Retirement requires:

- [ ] Resolve the master-side NIC receive timing issue on the intended host.
- [ ] Qualify SOES discovery, PREOP/SAFEOP/OP, reset/IDENTIFY, both command
  planes, retry/ack behavior, watchdog expiry, OP/link loss and recovery.
- [ ] Run fresh SOES target/feedback traffic for **more than 60 minutes without
  rebooting**, confirming continued commands and console uptime.
- [ ] Remove the TI-stack JW EtherCAT profile and its adapter/build assets.
- [ ] Make the default JW UART image UART-only, removing its TI EtherCAT demo
  dependency so current runtime images no longer link the evaluation slave stack.

Changing stacks alone does not resolve host timing issues; native tests and a
successful image build do not satisfy hardware qualification. TI SDK hardware
and PRU dependencies remain external inputs after slave-stack retirement.

SOES is GPLv2 with its upstream linking exception. TI's hardware interface and
PRU firmware retain their own licenses and remain external SDK inputs. This is
not a claim that every part of the AM243 firmware is open source or ready for
commercial redistribution.

## Host integration

The [JW EtherCAT config](../../../config/README.md#joshuawire-over-ethercat)
now selects paired adapters through CommFactory and the shared board engine.
Native integration tests run that entire path against this production firmware
core. Native coverage does not establish production Ethernet timing or
physical-output safety.

## Flash

Not automated by this target. After the image is built and reviewed, adapt the
existing TI demo flash configuration to point at:

```text
out/am243_dual_transport_rev4.release.appimage.hs_fs
```

Use the same SDK 09 bootloader/application offsets. Verify both bootloader and
application after flashing; do not use the vendor TI-demo application path for
a JoshuaWire image.

The separate JW EtherCAT profile uses
`out/am243_ethercat_jw.release.appimage.hs_fs`; it is not the UART image.
The SOES candidate uses `out/am243_ethercat_jw_soes.release.appimage.hs_fs`.

Flashing remains a deliberate hardware operation and must not happen as part
of build or test.

## Verify

Follow the [serial validation guide](../../../docs/JOSHUA_WIRE_VALIDATION.md)
using `//robot/board/joshua_wire:joshua_wire_smoke`. The old JW1 probe is removed.
For EtherCAT, build the separate JW profile and follow its configuration and
validation references above; the default UART image contains TI echo EtherCAT.

## Wiring / Pinout

- XDS110 UART console/protocol: 115200 8-N-1; the example uses `/dev/ttyACM0`.
  Check the actual port after connecting the board.
- EtherCAT: existing LP-AM243 IN/OUT ports and TI demo wiring.
- STEP/DIR pins are accepted and retained by the serial protocol but are not
  driven in this milestone.

## Known gaps / Troubleshooting

- In the `ti-demo` dual-transport artifacts, UART0 becomes a binary protocol stream after board initialization. Normal
  EtherCAT application logs are discarded after the serial task starts so
  they cannot corrupt frames; early bootloader logs may still appear before
  the first request and are flushed by Joshua's host transport.
- The dual-transport artifacts expose separate serial and EtherCAT demo state. A later command
  arbiter must unify them before either path controls the same physical motor.
  The separate JW EtherCAT artifact avoids simultaneous UART ownership.
- TI's bundled EtherCAT evaluation stack retains its one-hour runtime limit
  in the `ti-demo` and `jw` artifacts. The `jw-soes` candidate excludes that
  stack, but hardware/endurance qualification is required before retirement.
- No firmware is flashed automatically.

The UART artifact retains hardware-RNG boot identity and shared retained serial
transmission. Revision-4 frames larger than the 64-byte UART FIFO drain across
service iterations. Native builds/tests never flash devices.

An adapter-owned UART response overdue by 20 ms latches a local fault on the
next service iteration and disables/unconfigures the software actuator
independently of pending RX or available TX capacity. See the shared
[serial fault budget](../../common/README.md#serial-transmit-fault-budget).
