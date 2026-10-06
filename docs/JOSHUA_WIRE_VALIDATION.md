# JoshuaWire 0.0.2 validation

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
and never falls back to JW1. The older JW1 smoke tool is removed.

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
  --config=/path/to/reviewed-jw.pbtxt --board=board_name --dry_run
```

Invalid options and missing hardware confirmation are rejected before opening
the port. `--settle_ms` adds a diagnostic wait (default 2000 ms); set it to
zero to test only the configured runtime post-open delay.

## Hardware sequence (human-confirmed setup only)

1. Deliberately flash the matching [JW artifact](../firmware/README.md#joshuawire-serial).
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

JW 0.0.2 is the renamed validated JW2 protocol, with the same on-wire bytes.
The records below retain the actual historical artifact names and hashes;
current build and tool names appear above and in the firmware READMEs.
The operator reconfirmed serial validation on all three boards and EtherCAT
validation on AM243 on 2026-10-05. This does not extend the recorded bench scope.

These are limited bench results, not production safety or timing qualification.
All serial checks used the Ubuntu 24.04/Jazzy host container.

| Board/path | Protocol checks | Powered motion | Remaining |
| --- | --- | --- | --- |
| AM243 UART | Passed | Software-only channel | Physical backend, ROS 2 integration |
| Teensy 4.1 native USB | Passed | Forward/return observed | ROS 2 integration, pulse timing, link-loss safety |
| ESP32 CP2102 UART | Passed after reopening | Not tested | Initial post-upload stale replies, powered motion, ROS 2 integration, pulse timing, link-loss safety |
| AM243 EtherCAT/SOES | Bring-up only | Software-only channel | Timing/failure/endurance qualification |

### Recorded AM243 hardware result — 2026-09-27

Host/firmware source: `4d594d123ac72fdc8f83aa05fb607c9aebc3975a`.
Artifact: `am243_dual_transport_v2.release.appimage.hs_fs`, SHA-256
`d8612bb2e981a69cadc4e7fce94d31cc76270508927d51ac227dd8efabfa05d2`.
Bootloader/application flash verification passed.

XDS110 UART at 115200 baud identified `am243-dual-v2`, board ID 1, one
STEP_DIR channel. Eight sessions passed: two handshake, two configure-only,
and four exercise sessions at +250/-250 native steps. Feedback matched targets
with zero faults. Fresh resets returned position zero; every session acknowledged
ESTOP, and exercise sessions acknowledged DISABLE. Separate invocations reopened
the port. This image has no motor GPIO backend; external motor power was not
independently verified. EtherCAT, watchdogs and ROS 2 were not tested.

### AM243 JW 0.0.2 UART reflash — 2026-10-05

The operator confirmed the connected LP-AM243 in UART flashing mode and
power-cycled it; a fresh read on XDS110 `/dev/ttyACM0` showed the ROM `C`
prompt. Firmware source is commit `ac1c960ae3f19ddf3cb009aab950d4e339fc9240`
with documentation-only working-tree changes. The Ubuntu 24/Jazzy container
built the default UART/TI-demo image using Industrial Communications SDK
09.00.00.03 and the configured external TI tools.

Application: `am243_dual_transport_jw.release.appimage.hs_fs`, SHA-256
`ff5eca884d47c9c1ad3115c8eea0cbe487b9bee52338064e2c03fd9abc7d0c7e`.
SDK OSPI bootloader SHA-256:
`2224299731e4db89aa67637ae0e73b60d408a42a420979196281e1af8c42dd11`.
TI `uart_uniflash.py` loaded the flash writer, wrote PHY tuning data, then
flashed and verified the bootloader at `0x0` and application at `0x80000`.
All operations succeeded; external SDK files were not changed.

After the operator restored OSPI boot mode and power-cycled, the production
`joshua_wire_smoke` identified `am243-dual`, board ID 1 and one STEP_DIR
channel on XDS110 serial `S24L0464`, `/dev/ttyACM0`, at 115200 baud. Ten fresh
sessions across five physical port openings passed: two first-connection
handshake, two configure-only, two exercise at +250 native steps, two exercise
at -250, then two handshake after reopening. All ten RESET/IDENTIFY/ESTOP
sequences, six configurations, four ENABLE and four DISABLE commands succeeded.
All ten feedback replies had zero faults and velocity zero; the four target
feedback replies matched +250/+250/-250/-250. Fresh resets returned position
zero. Runtime exchange timeout was 100 ms with no configured or extra
post-open delay. No stale-reply failure was observed in this sample.

This image's channel is software-only and drives no motor GPIOs; target
feedback is software state, not measured motion. Final ESTOP was acknowledged
and all probe ports closed. The UART image retains TI's demo EtherCAT profile;
the separate JW EtherCAT artifact was neither flashed nor tested in this run.
Endurance, malformed/stale hardware requests and communication-loss behavior
were not tested. Original bench captures were temporary local files, not
repository artifacts.

### Recorded Teensy 4.1 hardware result — 2026-10-04

Host/firmware source: `a62eaea618d02e3d5a4a88f3bb8663f4eac0e499`.
Artifact: `teensy41-serial-v2` HEX, SHA-256
`88300e3eb0b0027a26a246f1bdfe45c173c4263be8d11b0c6ee118c816935f73`.
Build: PlatformIO 6.1.18, Teensy platform 5.2.0, Arduino framework 1.162.0.
The board initially ran v1; an approved flash via HalfKay succeeded after
manual PROGRAM-button entry.

Native USB identified `teensy-serial-v2`, board ID 2, one STEP_DIR channel.
Motor power was confirmed disconnected. Channel 0 used STEP/DIR/ENABLE pins
2/3/4, 4000 Hz maximum, 20 µs pulse width and active-low enable. Eight sessions
passed: two handshake, two configure-only, two exercise at +10 steps, then two
handshake after reopening. All stops were acknowledged and feedback faults
were zero. Immediate exercise counts were 1 and 2, not final target arrival.
Fresh resets retained the step count. Runtime exchange timeout was 100 ms,
with no post-open or extra diagnostic wait.

#### Powered Teensy motor bench — 2026-10-04

With separately confirmed powered hardware, a temporary bench used CommFactory,
JoshuaWireV2Session and shared codecs. At 100 pulses/s it held the initial count,
enabled, moved 89 steps forward, waited two seconds, returned and disabled.
Each leg had a three-second arrival deadline. Counts went 2 → 91 → 2; each leg
took approximately 0.89 seconds. The operator observed smooth forward-and-return
motion. Final DISABLE/ESTOP succeeded; the disabled count stayed stable.
This was not a ROS 2/motor-driver test or an independent angle/pulse measurement.

### Recorded Teensy 4.1 JW 0.0.2 reflash — 2026-10-05

The operator confirmed Teensy connected and requested flashing before testing.
Source: clean commit `ac1c960ae3f19ddf3cb009aab950d4e339fc9240` before adding
this result. PlatformIO built `teensy41-serial` with Teensy platform 5.2.0,
Arduino framework 1.162.0 and ARM GCC 15.2.1. Application HEX SHA-256:
`a11e17c2f93fe0607c9592eb3908a03864194ed0fd84a9c22a182cb646ad0380`.
Two uploads completed through HalfKay using Teensy Loader CLI 2.2.

The Ubuntu 24/Jazzy production `joshua_wire_smoke` identified `teensy-serial`,
board ID 2 and one STEP_DIR channel on native USB `/dev/ttyACM0`, USB serial
`15104350`. Eight sessions across four physical port openings passed:
two handshake sessions after each upload, two configure-only sessions, then
two handshake sessions after reopening. All eight RESET/IDENTIFY/ESTOP
sequences succeeded. Configuration used pins 2/3/4, maximum 4000 Hz, 20 µs
pulse width and active-low enable. Both feedback replies reported position 0,
velocity 0 and faults 0. The runtime exchange timeout was 100 ms, with no
configured or extra post-open wait. Neither first connection reproduced the
ESP32 stale-reply issue; these two observations do not establish endurance or
exhaustive startup qualification.

No ENABLE or SET_TARGET commands were sent. Motor-power state was not newly
confirmed in this run; powered motion was not tested. Final ESTOP was
acknowledged and the port closed.

The repeat upload required restarting the uploader after entry into HalfKay.
Probes began after native USB serial re-enumerated. Original bench captures
were temporary local files, not repository artifacts.

### Recorded ESP32 hardware result — 2026-10-04

Host/firmware source: `a62eaea618d02e3d5a4a88f3bb8663f4eac0e499`.
Artifact: `esp32-serial-v2` BIN, SHA-256
`aa6cbf9a03c34fbc69c42e6a420e58b7aa10548aaf04fd6729cec457cf2a0335`.
Build: PlatformIO 6.1.18, Espressif32 7.1.3, Arduino framework
`4.20017.260907+sha.dcc1105b`, Xtensa GCC `8.4.0+2021r2-patch5`.
The previous application did not answer either probe; bootloader identification
confirmed ESP32-D0WD-V3 revision v3.1. Approved upload with esptool 4.11.0
verified all transferred images and reset the board through RTS.

CP2102 UART at 115200 baud identified `esp32-serial-v2`, wire board ID 8,
one STEP_DIR channel. Motor power was confirmed disconnected. Channel 0 used
GPIOs 25/26/27, 4000 Hz maximum, 20 µs pulse width and active-low enable.
Eight sessions passed: two handshake, two configure-only, two exercise at +10
steps, then two handshake after reopening. All stops were acknowledged and
feedback faults were zero. Both exercise replies reported count 10; the second
session accepted the same target, not a second displacement. Fresh resets
retained the count. Runtime exchange timeout was 100 ms; configured post-open
settling was 2000 ms with no extra diagnostic wait. Powered motion was not tested.

### Recorded ESP32 JW 0.0.2 reflash — 2026-10-05

The operator confirmed the connected ESP32 setup with motor power disconnected.
The JW consolidation working tree (based on `15f1aca`) built and uploaded
`esp32-serial` using PlatformIO and esptool 4.11.0. The bootloader identified
ESP32-D0WD-V3 revision v3.1; esptool verified all transferred image hashes.
Input application BIN SHA-256:
`ab513c736e849451c70246e5178593b5726205cdf3c321ce3e011b188b78aa67`.

The production `joshua_wire_smoke` in Ubuntu 24/Jazzy identified
`esp32-serial`, wire board ID 8, and one STEP_DIR channel on CP2102
`/dev/ttyUSB0`. The final sequence passed eight fresh sessions across four
physical port openings: two handshake, two configure-only, two exercise with
a zero-step hold target, then two handshake after reopening. Both ENABLE and
DISABLE replies and all eight ESTOP cleanups were acknowledged; all feedback
reported position/velocity zero and no faults. GPIOs 25/26/27, 4000 Hz maximum,
20 µs pulse width, active-low enable, 100 ms exchange timeout and the configured
2000 ms post-open settle were used. Extra diagnostic settle was zero.
Powered motion and physical pulse timing were not tested.

**Post-upload caveat:** the first port open after each of three uploads failed
correlation after a successful RESET. A syscall trace captured stale replies
for reset/message ID 1 in response to IDENTIFY/message ID 2 and ESTOP/message
ID 3. No ENABLE or target commands were sent in those failed probes, and ESTOP
was unconfirmed. Closing/reopening recovered; the complete eight-session
sequence passed twice with the same BIN. Uploading at 115200 instead of the
default 460800 also reproduced the failure, so upload speed was not a fix.
The cause remains unresolved. No automatic retry, correlation bypass, runtime
setting change or upload-speed change was adopted. Initial post-upload
reliability is not qualified by the subsequent passing sessions.

Serial results above do not cover manual power-cycle recovery, stale-ID/retry
hardware probes, endurance, communication-loss safety or independent GPIO timing.
Each completed probe closed its port.

### ESP32 post-upload issue retest — 2026-10-05

Six additional uploads of the same BIN (SHA-256 above) used the default
460800 upload baud, followed immediately by the production handshake probe.
Motor power remained disconnected; only RESET_SESSION, IDENTIFY and ESTOP
were sent. The exchange timeout remained 100 ms.

| Total post-open wait | First connections | Passed | Failed correlation |
| --- | --- | --- | --- |
| Configured 2000 ms, no extra wait | 4 | 2 | 2 |
| Configured 2000 ms + diagnostic 4000 ms | 2 | 2 | 0 |

One normal-wait failure occurred without syscall tracing; the other was
captured with timestamped `strace`. The captured run transmitted message IDs
1/2/3 for RESET/IDENTIFY/ESTOP, but received the same CRC-valid RESET reply
for message ID 1 each time. The RESET exchange also read 1620 bytes preceding
the valid reply. This reproduces the earlier failure and does not establish
whether the stale replies originate in firmware or the UART/USB receive path.
Traced and untraced first connections both passed in other trials.

After each failure, closing/reopening passed two fresh handshake sessions
with the normal wait. The final recovery acknowledged ESTOP and closed the
port. The two longer-wait passes do not qualify a timing workaround: normal-wait
attempts also passed intermittently. No code, preset timing, correlation policy
or upload-setting changes were made for this retest; the cause remains unresolved.
Original syscall traces and bench captures were temporary local files, not
repository artifacts.

### ESP32 reconnect and byte-capture retest — 2026-10-05

After the operator reconnected ESP32, two production handshake sessions passed
before uploading. Two further uploads of the same BIN (SHA-256 above), at
default 460800 upload baud, each immediately preceded a first-connection
production probe. One first connection passed; the other acknowledged RESET
but rejected uncorrelated IDENTIFY/ESTOP replies. Both used the configured
2000 ms post-open wait, no extra wait and 100 ms exchange deadline. Firmware
and host source: `ac1c960`, with documentation-only working-tree changes.

A third upload preceded an independent temporary Python diagnostic that sent
one RESET, one IDENTIFY and one ESTOP, with fresh session/message IDs and valid
CRC. It read in chunks throughout a 100 ms window per request, rather than
returning at the first frame. The RESET window captured 26265 bytes containing
1541 identical CRC-valid RESET replies for the new session/message ID 1.
The IDENTIFY window captured 70 more of those RESET replies followed by one
correctly correlated IDENTIFY reply for message ID 2. The ESTOP window contained
one correctly correlated ESTOP reply for message ID 3. The diagnostic marked
IDENTIFY failed because its first valid reply was uncorrelated; finding a later
matching reply did not turn that result into a pass.

This independently reproduces duplicated/stale received data while also showing
that a valid IDENTIFY reply can arrive behind it. It does not establish whether
duplication starts in firmware, the bridge or the host receive path. Chunked
reads and full-window collection alter the diagnostic's timing; it is not a
production transport qualification or adopted workaround.

Closing/reopening afterward passed two fresh production handshake sessions.
Final ESTOP was acknowledged and the port closed. All checks sent only RESET,
IDENTIFY and ESTOP; no configuration, enable or target commands were sent.
No code, preset timing or correlation policy changed. The diagnostic script,
logs and traces were temporary local files, not repository artifacts.

## Recorded AM243 EtherCAT result — 2026-09-28

Artifact: `am243_ethercat_jw2.release.appimage.hs_fs`, SHA-256
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

Artifact: `am243_ethercat_jw2_soes.release.appimage.hs_fs`, SHA-256
`424c7d498e35efcbdd0c5d27dc490d0c6dd8e3b81896520bc170c36a1ef10b83`.
Source: working tree based on `7f33853`, not a clean release validation.
Approved flash verification passed. This image excludes TI's evaluation slave
stack but retains external TI hardware/PRU dependencies. Watchdogs remained
2 s / 1 s, and motor power was confirmed disconnected.

Discovery returned `Joshua AM243 JW2 SOES`, valid `am243-soes2` descriptor,
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
