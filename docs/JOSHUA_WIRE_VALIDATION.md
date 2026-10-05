# JoshuaWire validation

Use `//robot/board/joshua_wire:joshua_wire_smoke` for serial checks on AM243,
Teensy 4.1 and ESP32. It uses the production host session and serial transport,
but does not launch ROS 2, flash firmware or test EtherCAT.

## Safety and scope

Follow the [hardware-safety rules](../AGENTS.md#hardware-safety--read-this-first).
Start with motor power disconnected, a secured mechanism and no other process
using the port. Even the handshake changes hardware state: opening serial may
reset the MCU, RESET_SESSION disables all channels and clears configuration,
and cleanup sends board-wide ESTOP. Loss of holding torque can be hazardous.

`--confirm_hardware` acknowledges setup; it is not a wiring check or interlock.
The probe opens only the selected board and does not honor
`general.operation_mode`. It requires explicit `protocol: JOSHUA_WIRE`
and never falls back to v1. Older board-specific smokes use v1 and send targets.

Serial firmware has no communication-loss watchdog. Cleanup attempts ESTOP after
command failure or SIGINT/SIGTERM, but a lost link, crash or SIGKILL can prevent
it. A timeout means the command outcome may be unknown. Keep a physical
motor-power cutoff available; USB disconnect is not an emergency stop.
Targets are absolute native steps, bypassing actuator conversions and joint
limits. Feedback counts commanded pulses, not measured motor movement.

## Build and preflight (no hardware access)

Inside the [Docker development environment](../CONTRIBUTING.md#development-setup):

```bash
bazel build --config=u24 --config=x86-base \
  --@rules_python//python/config_settings:python_version=3.12 \
  //robot/board/joshua_wire:joshua_wire_smoke
```

For Humble, use `u22` and Python `3.10`. Prepare a local board configuration
with `protocol: JOSHUA_WIRE` and `firmware { min_proto_version: 2 }`.
Review the device path, identity, channel pins and [serial timing](../config/README.md#serial-timing).

```bash
bazel-bin/robot/board/joshua_wire/joshua_wire_smoke \
  --config=/path/to/reviewed-v2.pbtxt --board=board_name --dry_run
```

Invalid options and missing hardware confirmation are rejected before opening
the port. `--settle_ms` adds a diagnostic wait (default 2000 ms); set it to
zero to test only the configured runtime post-open delay.

## Hardware sequence (human-confirmed setup only)

1. Deliberately flash the matching [v2 artifact](../firmware/README.md#opt-in-joshuawire-v2-serial-milestone).
   AM243 UART has a software-only channel; Teensy/ESP32 can drive GPIO.
2. Replace `--dry_run` with `--confirm_hardware`. The default handshake runs
   RESET_SESSION, IDENTIFY and ESTOP, without configuring or enabling channels.
3. Add `--mode=configure --channel=0`, first as a dry-run. This configures the
   channel and reads feedback without enabling it. Configuration writes GPIO
   on Teensy/ESP32, so review pins even with motor power disconnected.
4. With motor power disconnected, optionally add `--mode=exercise --channel=0
   --allow_enable --target_steps=<reviewed-absolute-target>`. The probe sets a
   hold target, enables, sends the target, reads feedback once, disables and
   ESTOPs. It does not wait for arrival or test a trajectory.
5. Use `--sessions=2` for fresh sessions on one open port. For reboot/reconnect
   recovery, close the probe, safely reboot/reconnect, verify the enumerated
   port and start a new probe.

Powered motion needs separate approval. Record source commit, firmware artifact
hash, board, timing, power state, commands and results. Keep temporary bench
programs, configurations and captures outside the repository.

## Hardware validation status

These are limited bench results, not production safety or timing qualification.
All serial checks used the Ubuntu 24.04/Jazzy host container.

| Board/path | Protocol checks | Powered motion | Remaining |
| --- | --- | --- | --- |
| AM243 UART | Passed | Software-only channel | Physical backend, ROS 2 integration |
| Teensy 4.1 native USB | Passed | Forward/return observed | ROS 2 integration, pulse timing, link-loss safety |
| ESP32 CP2102 UART | Passed | Not tested | Powered motion, ROS 2 integration, pulse timing, link-loss safety |
| AM243 EtherCAT/SOES | Bring-up only | Software-only channel | Timing/failure/endurance qualification |

### Recorded AM243 hardware result — 2026-09-27

Host/firmware source: `4d594d123ac72fdc8f83aa05fb607c9aebc3975a`.
Artifact: `am243_dual_transport_v2.release.appimage.hs_fs`, SHA-256
`d8612bb2e981a69cadc4e7fce94d31cc76270508927d51ac227dd8efabfa05d2`.
Bootloader/application flash verification passed.

XDS110 UART at 115200 baud identified `am243-dual`, board ID 1, one
STEP_DIR channel. Eight sessions passed: two handshake, two configure-only,
and four exercise sessions at +250/-250 native steps. Feedback matched targets
with zero faults. Fresh resets returned position zero; every session acknowledged
ESTOP, and exercise sessions acknowledged DISABLE. Separate invocations reopened
the port. This image has no motor GPIO backend; external motor power was not
independently verified. EtherCAT, watchdogs and ROS 2 were not tested.

### Recorded Teensy 4.1 hardware result — 2026-10-04

Host/firmware source: `a62eaea618d02e3d5a4a88f3bb8663f4eac0e499`.
Artifact: `teensy41-serial` HEX, SHA-256
`88300e3eb0b0027a26a246f1bdfe45c173c4263be8d11b0c6ee118c816935f73`.
Build: PlatformIO 6.1.18, Teensy platform 5.2.0, Arduino framework 1.162.0.
The board initially ran v1; an approved flash via HalfKay succeeded after
manual PROGRAM-button entry.

Native USB identified `teensy-serial`, board ID 2, one STEP_DIR channel.
Motor power was confirmed disconnected. Channel 0 used STEP/DIR/ENABLE pins
2/3/4, 4000 Hz maximum, 20 µs pulse width and active-low enable. Eight sessions
passed: two handshake, two configure-only, two exercise at +10 steps, then two
handshake after reopening. All stops were acknowledged and feedback faults
were zero. Immediate exercise counts were 1 and 2, not final target arrival.
Fresh resets retained the step count. Runtime exchange timeout was 100 ms,
with no post-open or extra diagnostic wait.

#### Powered Teensy motor bench — 2026-10-04

With separately confirmed powered hardware, a temporary bench used CommFactory,
JoshuaWireSession and shared codecs. At 100 pulses/s it held the initial count,
enabled, moved 89 steps forward, waited two seconds, returned and disabled.
Each leg had a three-second arrival deadline. Counts went 2 → 91 → 2; each leg
took approximately 0.89 seconds. The operator observed smooth forward-and-return
motion. Final DISABLE/ESTOP succeeded; the disabled count stayed stable.
This was not a ROS 2/motor-driver test or an independent angle/pulse measurement.

### Recorded ESP32 hardware result — 2026-10-04

Host/firmware source: `a62eaea618d02e3d5a4a88f3bb8663f4eac0e499`.
Artifact: `esp32-serial` BIN, SHA-256
`aa6cbf9a03c34fbc69c42e6a420e58b7aa10548aaf04fd6729cec457cf2a0335`.
Build: PlatformIO 6.1.18, Espressif32 7.1.3, Arduino framework
`4.20017.260907+sha.dcc1105b`, Xtensa GCC `8.4.0+2021r2-patch5`.
The previous application did not answer either probe; bootloader identification
confirmed ESP32-D0WD-V3 revision v3.1. Approved upload with esptool 4.11.0
verified all transferred images and reset the board through RTS.

CP2102 UART at 115200 baud identified `esp32-serial`, wire board ID 8,
one STEP_DIR channel. Motor power was confirmed disconnected. Channel 0 used
GPIOs 25/26/27, 4000 Hz maximum, 20 µs pulse width and active-low enable.
Eight sessions passed: two handshake, two configure-only, two exercise at +10
steps, then two handshake after reopening. All stops were acknowledged and
feedback faults were zero. Both exercise replies reported count 10; the second
session accepted the same target, not a second displacement. Fresh resets
retained the count. Runtime exchange timeout was 100 ms; configured post-open
settling was 2000 ms with no extra diagnostic wait. Powered motion was not tested.

Serial results above do not cover manual power-cycle recovery, stale-ID/retry
hardware probes, endurance, communication-loss safety or independent GPIO timing.
Each completed probe closed its port.

## Recorded AM243 EtherCAT result — 2026-09-28

Artifact: `am243_ethercat_jw.release.appimage.hs_fs`, SHA-256
`beddbbbca07849c7788c5862684b4b9664e547a67eb5a38ddc06ec0020f8e5b6`.
Source: working tree based on `3e9f268`, not a clean release validation.
The approved flash verified successfully. The AM243 software-only channel ran
with motor power disconnected, command-progress watchdog 2 s and target watchdog
1 s.

The production BoardFactory/CommFactory/engine/paired CoE/PDO path passed
discovery, reset, identity, configuration, alternating +42/-42 targets and
feedback, DISABLE and ESTOP. Discovery reported 80/80-byte PDO mappings and a
valid `am243-ec-v2` descriptor. Stale-target feedback latched fault `0x2`;
ENABLE/SET_TARGET were rejected until fresh-session recovery. Retained handles
rejected commands after teardown; separate processes reopened the NIC.

A 1 ms host deadline failure left command/stop outcomes unknown. Four later
sessions passed with a temporary 20 ms cycle, 5 ms process/state/mailbox budgets,
2 ms guard and 1 s operation/response timeouts. Longer runs failed at both 1 ms
and 5 ms; these values are not validated production recommendations.
No firmware-side stop was independently observed during the host failures.
The shorter target watchdog also prevented independent progress-watchdog
qualification. Link/OP loss, multi-slave hardware, ROS 2 and physical-output
safety remain untested.

## SOES candidate bring-up — 2026-09-28

Artifact: `am243_ethercat_jw_soes.release.appimage.hs_fs`, SHA-256
`424c7d498e35efcbdd0c5d27dc490d0c6dd8e3b81896520bc170c36a1ef10b83`.
Source: working tree based on `7f33853`, not a clean release validation.
Approved flash verification passed. This image excludes TI's evaluation slave
stack but retains external TI hardware/PRU dependencies. Watchdogs remained
2 s / 1 s, and motor power was confirmed disconnected.

Discovery returned `Joshua AM243 JW SOES`, valid `am243-soes2` descriptor,
80/80-byte PDOs and bench vendor/product/revision
`0xe000059d/0x4a570002/0x00020002`; these are not registered product IDs.
Bring-up exposed and fixed a generic host CoE bug: slave reply counters advance
independently and need not echo request counters. Reset, identity, configuration
and teardown then passed. ENABLE attempts still hit 5 ms register deadlines,
leaving command/stop outcomes unknown. Fresh sessions recovered.

### Receive-path controls and decision to defer qualification

The bench master's Realtek RTL8125/`r8169` receive path is the leading suspect.
With unchanged SOES firmware, continuous NAPI polling completed two runs of 500
disabled-channel feedback calls with successful initialization/teardown.
Restoring normal reception reproduced timeouts. Disabling IRQ deferral and
software coalescing did not fix the stalls. Captures saw matching replies
arrive just after the deadline/next transmission; without hardware timestamps,
their wire arrival and the exact defect remain unproven.

Polling consumed roughly one CPU core and was diagnostic only. All NIC settings
were restored; no polling or timeout workaround was adopted. Qualification is
deferred pending a suitable NIC. Disabled feedback passes do not qualify SOES
target handling, watchdogs, link/OP loss or the continuous-over-one-hour
evaluation-stack retirement gate. Original temporary captures are not durable
repository artifacts. See [transport limitations](../robot/comm/ethercat/README.md#known-master-side-nic-timing-issue).

## Automated coverage

`serial_validation_test` exercises the real AM243 software handler in memory.
`joshua_wire_smoke_test` runs the CLI against it over a Linux pseudo-terminal,
covering handshake, configure/exercise, sessions, safety gates, lost replies,
correlation and SIGTERM cleanup. Session/transport tests cover framing, stale
replies, firmware retries, ID rotation and teardown. Native tests use no hardware
and cannot establish motor safety or pulse timing.

Run the CLI test inside the matching dev container:

```bash
bazel test --config=u24 --config=x86-base \
  --@rules_python//python/config_settings:python_version=3.12 \
  //robot/board/joshua_wire:joshua_wire_smoke_test
```

Full regression: `docker compose run --rm test-u22` and `test-u24`.
