# JoshuaWire v2 validation

The procedure below is for serial. The separate EtherCAT bench result and its
limits are recorded under [AM243 EtherCAT](#recorded-am243-ethercat-result--2026-09-28).

The manual `//robot/board/joshua_wire:joshua_wire_v2_smoke` tool checks AM243,
Teensy 4.1 and ESP32 through the production host v2 session and serial transport.
It does not launch ROS 2 nodes, flash firmware or validate EtherCAT. The older
board-specific smoke tools remain v1-only and automatically send targets.

The tool itself can be validated without hardware using the
[automated CLI test](#automated-coverage), which supplies simulated firmware
over a pseudo-terminal. Physical-board validation is a separate step.

## Safety and scope

Follow the [hardware-safety rules](../AGENTS.md#hardware-safety--read-this-first).
Even the default handshake is **not read-only**: opening serial may reset the
board, RESET_SESSION disables all channels and clears configuration, and exit
sends board-wide ESTOP. Loss of holding torque can also be hazardous. Secure
the mechanism and start with motor power disconnected; stop other processes
using the port. `--confirm_hardware` is an operator acknowledgment, not a wiring
check, port lock or safety interlock.

The tool opens only the named board from `config.robot.boards`. It does not
honor `general.operation_mode` or start other configured devices. Port, baud
and pin settings come from that board's protobuf config. It requires explicit
`protocol: JOSHUA_WIRE_V2`; it never upgrades presets or falls back to v1.

There is no communication-loss watchdog yet. Cleanup attempts ESTOP after a
command failure or SIGINT/SIGTERM, once the current bounded exchange returns.
A lost link, crash or SIGKILL can prevent cleanup. A timeout can mean that the
command executed but its reply was lost. Do not use disconnect as an emergency
stop. The tool bypasses actuator conversions and joint limits; targets are
absolute native steps, not degrees. Feedback is not proof of physical motion.

## Build and preflight (no hardware access)

Use the [Docker development environment](../CONTRIBUTING.md#development-setup).
Inside the matching dev container, build the tool:

```bash
bazel build --config=u24 --config=x86-base \
  --@rules_python//python/config_settings:python_version=3.12 \
  //robot/board/joshua_wire:joshua_wire_v2_smoke
```

Use `u22` and Python `3.10` for the Humble container. Prepare a local copy of
your board preset with explicit v2 selection and `firmware { min_proto_version:
2 }`; review the port, board type and channel wiring. Then run **only dry-run**
until the setup has been confirmed:

```bash
bazel-bin/robot/board/joshua_wire/joshua_wire_v2_smoke \
  --config=/path/to/reviewed-v2.pbtxt --board=board_name --dry_run
```

`--help` describes all flags. Invalid config/options and missing hardware
confirmation are rejected before opening the port. `--settle_ms` defaults to
2000 for USB-serial boot settling; it is a diagnostic allowance, not a change
to runtime transport policy.

## Hardware sequence (human-confirmed setup only)

1. Build and deliberately flash the matching [v2 artifact](../firmware/README.md#opt-in-joshuawire-v2-serial-milestone).
   Start with AM243 UART: its current channel is software-only and does not
   drive GPIO. This does not validate its TI EtherCAT demo or a motor backend.
2. Run the default handshake with `--confirm_hardware` instead of `--dry_run`.
   Expect RESET_SESSION, matching board identity/firmware name, then ESTOP OK.
   No CONFIGURE_CHANNEL, ENABLE or SET_TARGET is sent.
3. Add `--mode=configure --channel=0`, first with `--dry_run`, then with explicit
   hardware confirmation. Only the selected channel is configured; feedback is
   read, but no enable or target is sent. Configuration itself writes GPIO on
   Teensy/ESP32, so verify wiring even with motor power disconnected.
4. With the software-only AM243 channel, or with Teensy/ESP32 motor power
   disconnected, optionally use `--mode=exercise --channel=0 --allow_enable
   --target_steps=<reviewed-absolute-step-target>`. All flags are required, plus
   hardware confirmation. The tool sets a hold target, enables, sends the
   requested target, reads feedback once, disables and sends ESTOP. It does not
   wait for target arrival or perform a controlled motion/trajectory test.
5. Use `--sessions=2` to check fresh session resets on the same open port. For
   actual reboot/reconnect checks, exit, reboot/reconnect the board while the
   mechanism is safe and motor power is disconnected, then start a new probe.
   Reconfirm the enumerated port. The next probe must establish a fresh session.

Real powered motion is a separate, explicitly approved test, not a completion
criterion for this protocol probe. Record host commit, firmware artifact/commit,
board, mode, wiring/power state and command output. A successful run confirms
only the selected protocol operations, not watchdogs, GPIO timing, ROS 2
integration or the final board/comm separation architecture.

## Recorded AM243 hardware result — 2026-09-27

LP-AM243 UART validation passed using host and firmware source commit
`4d594d123ac72fdc8f83aa05fb607c9aebc3975a` (Ubuntu 24.04/Jazzy host).
The rebuilt `am243_dual_transport_v2.release.appimage.hs_fs` had SHA-256
`d8612bb2e981a69cadc4e7fce94d31cc76270508927d51ac227dd8efabfa05d2`.
TI bootloader and application flash verification both succeeded. After the
operator changed SW4 to normal flash boot and power-cycled, the existing probe
used XDS110 UART `/dev/ttyACM0` at 115200 baud and identified `am243-dual-v2`,
board ID 1, one STEP_DIR channel.

Eight sessions passed: two handshake, two configure-only, two exercise at
`+250` native steps, and two exercise at `-250`. Configuration used the example
AM243 values (pins 2/3/4, 4000 Hz, 20 us, active-low enable), retained only in
software by this image. Feedback matched each target with zero reported faults;
fresh sessions returned initial position zero. Every session ended with a
successful ESTOP; exercise sessions also acknowledged DISABLE. The port was
closed at completion. Each pair used one open port; separate probe invocations
also exercised reopening it.

This was a real UART/firmware test, **not physical motion**: the flashed handler
has no motor GPIO backend. External motor-power state was not independently
verified. EtherCAT, watchdogs, power loss during a command, pulse timing and
ROS 2 integration were not tested. Temporary configs/logs were kept outside
Git; no additional smoke tool was added.

## Recorded Teensy 4.1 hardware result — 2026-10-04

Native USB serial validation passed using host and firmware source commit
`a62eaea618d02e3d5a4a88f3bb8663f4eac0e499` (Ubuntu 24.04/Jazzy Docker).
The connected board initially answered a CRC-valid v1 IDENTIFY request; the
v2 reset probe rejected its non-v2 response. After separate operator approval,
`teensy41-serial-v2` was rebuilt and flashed. Automatic bootloader entry failed;
the operator pressed PROGRAM, and a second upload found HalfKay, programmed
successfully and rebooted into the application. Firmware HEX SHA-256:
`88300e3eb0b0027a26a246f1bdfe45c173c4263be8d11b0c6ee118c816935f73`.
Build tools were PlatformIO Core 6.1.18, Teensy platform 5.2.0 and Arduino
framework 1.162.0; no tracked firmware source changes were needed.

The production host session/framed-serial path identified `teensy-serial-v2`,
board ID 2, one STEP_DIR channel, through
`/dev/serial/by-id/usb-Teensyduino_USB_Serial_15104350-if00` (`/dev/ttyACM0`),
configured at 115200 baud (native USB ignores baud). Motor power was
operator-confirmed disconnected and the serial port free. Channel 0 used the
reference pins STEP 2, DIR 3, ENABLE 4, 4000 Hz maximum, 20 µs pulse width and
active-low enable. Runtime timing defaults remained unchanged: 100 ms exchange
timeout and no post-open settle delay. Every successful probe also used
`--settle_ms=0`, without the diagnostic tool's extra default wait.

Eight sessions passed: two handshake, two configure-only, two exercise with an
absolute target of +10 native steps, then two handshake after reopening the
port. Every session acknowledged RESET_SESSION, IDENTIFY and ESTOP; both
exercise sessions also acknowledged ENABLE, SET_TARGET and DISABLE. All
feedback reported zero faults. Immediate exercise feedback advanced from 0 to
1 and from 1 to 2 commanded steps; the probe deliberately does not wait for
arrival at +10. Fresh session reset clears configuration and stops outputs but
does not zero Teensy's accumulated step count. Each pair reused one open port;
separate probe processes also verified port reopening. The port was closed at
completion, with the last ESTOP acknowledged.

The initial eight sessions validate the selected real USB/firmware protocol
operations, **not powered motion, encoder feedback or GPIO pulse timing**.
They included no independent output-level measurement, ROS 2 run,
cable-loss/watchdog test, stale-ID/retry hardware probe, endurance run or
power-cycle recovery test. The firmware reboot
after flashing and subsequent fresh sessions were tested. Temporary configs,
identify-only probe, build/upload logs and session logs remain outside Git at
`/tmp/joshua-teensy-jw2.XBWH5t/`; no repository test utility was added.

### Powered Teensy motor bench — 2026-10-04

After separate operator confirmation of the powered motor setup, reference
TB6600 wiring, secured/unloaded motor and accessible physical power cutoff, a
temporary C++ bench used the same production CommFactory, framed serial adapter,
JoshuaWireV2Session and shared payload codecs. Firmware and host source commit
were unchanged from the serial result above. This was not a ROS 2/launcher or
motor-driver integration test. The bench executable, code, configuration and log
remain under `/tmp/joshua-teensy-jw2.XBWH5t/`, outside the repository.

Channel 0 retained pins 2/3/4, active-low enable and 20 µs pulses; the temporary
protobuf configuration reduced the maximum rate to 100 pulses/s. The sequence
reset and identified, configured while disabled, read the initial count and
set a hold target before enabling. It then commanded +89 native steps relative
to that count, polled feedback for arrival with a 3-second leg deadline, paused
2 seconds, returned to the initial count and disabled. At the reference 1/16
microstepping, 89 pulses correspond to approximately 10°; actual shaft angle
depends on the installed drive settings and any gearing.

The run passed all command/status checks: feedback went from 2 to 91 in
0.886474 seconds and back to 2 in 0.886530 seconds, with no reported faults.
The count remained 2 after a 200 ms disabled check. Final DISABLE and ESTOP
cleanup both returned OK, and the serial port closed. This remains commanded
step-count evidence, not encoder or independent pulse measurements. The
operator independently confirmed that the motor visibly moved forward, paused
and returned smoothly. This establishes a limited powered JW2 motion bench
pass, not measured shaft-angle accuracy, ROS 2 integration, communication-loss
safety or pulse-timing validation.

## Recorded ESP32 hardware result — 2026-10-04

UART/USB-bridge validation passed using host and firmware source commit
`a62eaea618d02e3d5a4a88f3bb8663f4eac0e499` (Ubuntu 24.04/Jazzy Docker).
The connected board initially timed out on JW2 reset and did not return a
CRC-valid v1 IDENTIFY response at 115200 baud; its previous application version
was not established. A bootloader identification confirmed ESP32-D0WD-V3,
revision v3.1, through the CP2102 bridge. After separate operator approval,
`esp32-serial-v2` was rebuilt and uploaded to that exact port. The bootloader,
partition table, boot-app image and application transfers all passed esptool's
data-hash verification, followed by a hardware reset through RTS. Local
application BIN SHA-256:
`aa6cbf9a03c34fbc69c42e6a420e58b7aa10548aaf04fd6729cec457cf2a0335`.
Build tools were PlatformIO Core 6.1.18, Espressif32 platform 7.1.3, Arduino
framework `4.20017.260907+sha.dcc1105b`, Xtensa GCC `8.4.0+2021r2-patch5` and
esptool 4.11.0. No tracked firmware source changes were needed.

The production host session/framed-serial path identified `esp32-serial-v2`,
wire board ID 8, one STEP_DIR channel, at 115200 baud through
`/dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0`
(`/dev/ttyUSB0`). Motor power was operator-confirmed disconnected and the serial
port free. Channel 0 used the reference GPIOs 25/26/27, 4000 Hz maximum, 20 µs
pulse width and active-low enable. Exchange timing retained its 100 ms default;
the temporary protobuf configuration explicitly set `post_open_settle_ms: 2000`.
Each probe used `--settle_ms=0`, so the only post-open wait came from the
production serial configuration, not an extra diagnostic or board-specific delay.

Eight sessions passed: two handshake, two configure-only, two exercise at an
absolute target of +10 native steps, then two handshake after reopening the
port. Every session acknowledged RESET_SESSION, IDENTIFY and ESTOP; both
exercise sessions also acknowledged ENABLE, SET_TARGET and DISABLE. Configure
feedback was position zero; both exercise replies reported position 10 and
zero faults. The second exercise session started at count 10 and accepted the
same hold/target value, so it did not demonstrate another displacement. Fresh
session reset clears configuration but retains the accumulated step count.
Each pair reused one open port, and separate processes also verified port
reopening. Final ESTOP was acknowledged and the port was closed.

These are real USB/UART and firmware command results, not powered motion,
encoder feedback, independent GPIO pulse measurements or ROS 2 integration.
No cable-loss/watchdog, stale-ID/retry hardware probe, endurance or manual
power-cycle recovery test was performed. The post-flash reset and subsequent
fresh sessions were tested. Temporary configs, identify-only probe and
build/upload/session logs remain outside Git at
`/tmp/joshua-esp32-jw2.iaWZjj/`; no new repository test utility was added.

## Recorded AM243 EtherCAT result — 2026-09-28

The separate `am243_ethercat_jw2.release.appimage.hs_fs` was flashed and verified
on LP-AM243, then booted from OSPI after the operator changed SW4 and power-cycled.
Application SHA-256:
`beddbbbca07849c7788c5862684b4b9664e547a67eb5a38ddc06ec0020f8e5b6`.
The SDK 09 OSPI bootloader was also flash-verified (SHA-256
`2224299731e4db89aa67637ae0e73b60d408a42a420979196281e1af8c42dd11`).
Host and firmware came from the working tree based on
`3e9f268b30c55e390497d551bb931267a86ec16b`, including then-uncommitted
CoE/PDO profile and factory integration changes; this is not a clean-commit
release validation. Host: Ubuntu 24.04/Jazzy Docker, dedicated NIC `enp5s0`,
one slave, split LRD/LWR. Motor power was operator-confirmed disconnected;
the image has no motor GPIO backend. Firmware watchdog intervals were explicitly
built as 2000000 µs command progress and 1000000 µs target freshness.

Discovery returned `Joshua AM243 JW2 software channel`, SII vendor `0xe000059d`,
product `0x4a570002`, revision `0x00020001`, and 640-bit input/output mappings
(80 bytes each, offsets zero). The 36-byte descriptor passed the host gate:
`am243-ec-v2`, protocol 2, layout 1, frame limit 64, transport bits 6.

A temporary probe exercised the production BoardFactory → CommFactory →
JoshuaWire engine → paired CoE/PDO path, without launching ROS nodes. Results:

- One initialization/teardown session passed reset, identity, configuration and
  ESTOP. Four successful exercise sessions each enabled the software channel,
  alternated ten targets between +42 and -42 native steps, received matching
  feedback with zero faults, disabled and tore down successfully.
- Two stale-target sessions passed. Fresh GET_FEEDBACK commands continued while
  no new target was supplied; feedback eventually reported position zero and
  fault `0x2`. ENABLE and SET_TARGET were rejected while latched; DISABLE and
  teardown succeeded. A subsequent fresh-session exercise cleared the fault
  and passed. This verifies behavior, not an exact measured trip latency.
- Retained channel handles rejected commands after every successful teardown.
  Separate processes reopened the NIC and established fresh sessions.

There was also **one failed exercise session**, not counted among those passes:
with a 20 ms period and 1 ms process/state/mailbox budgets and scheduling guard,
a mailbox datagram hit its deadline after SET_TARGET succeeded. The next feedback,
disable and teardown reported failure/unknown outcome; the next fresh-session
exercise recovered. No firmware-side stop was independently observed during this
failure. The logs do not distinguish packet delay/loss from host scheduling.

The final four sessions (three exercise and one watchdog) all passed with a
temporary bench policy of 20 ms period, 5 ms process/state/mailbox budgets, 2 ms
scheduling guard, and 1 s operation/response timeouts. This is a limited sample,
**not a production timing recommendation or a hard-real-time guarantee**. The
repository's illustrative timing values were not silently changed.

The probe source, configs and logs remain outside Git under
`/tmp/joshua-am243-ethercat-flash.epDDla/`; no new repository test utility was
added. Current gaps include long-duration/load timing, cable loss and OP-loss
observations, multi-slave real-bus behavior, independent command-progress timeout,
simultaneous transports, ROS 2 integration and physical motor safety. With the
installed 1 s target limit shorter than the 2 s progress limit, silence trips
the target watchdog first; it does not independently validate the latter.

### Follow-up timing investigation — 2026-09-28

Longer instrumented attempts **did not establish reliability at either 1 ms or
5 ms**. Six attempts ended in a register-datagram timeout: three at mailbox
read-status register `0x080d`, three at AL-state register `0x0130`. The previous
generic "mailbox" error could refer to either path. Four attempts used unchanged
transport behavior; one tried socket-local `PACKET_QDISC_BYPASS`, and one tried
explicit minimum Ethernet frame padding. Neither experiment eliminated failure;
neither was adopted in production. Attempts requested 500 target/feedback pairs,
but none completed that soak. Some failed during initialization.

Across those attempts, temporary bounded in-memory tracing recorded 1057
successful register datagrams (mean 31.0 µs, maximum 66.7 µs) and six failures
(about 1.01–1.03 ms or 5.02–5.10 ms, according to budget). Failed waits consumed
only 19–93 µs of thread CPU time. These are instrumented host observations, not
wire-time measurements or a scheduling guarantee.

A passive capture limited to EtherCAT on `enp5s0` caught two failures. Matching
replies with working count 1 appeared at the host approximately 1.071 ms and
5.067 ms after their outgoing packets, just after the master sent shutdown PDOs.
Thus those replies were late, not permanently absent. Kernel receive timestamps
cannot distinguish a wire/slave delay from delayed NIC/driver receive processing;
the correlation with subsequent transmit is evidence to investigate, not proof
of its cause. A simple application-thread scheduling explanation is insufficient
without accounting for that receive-path evidence.

The host NIC reported Realtek `r8169`, firmware `rtl8125d-1_0.0.7`, kernel
`7.0.0-34-generic`, 100 Mb/s link. NIC errors/missed packets and qdisc drops were
zero; EEE was enabled but inactive. No system-wide NIC settings, timeout defaults
or firmware were changed. Capture, instrumentation and experimental variants
remain under the same temporary directory, outside Git.

Production changes only improve diagnostics: register errors include command,
station, register, size, timing budget, elapsed time and working count; the master
preserves that detail when reporting a backend overrun. One regression was added
to the existing test file. Both Compose suites passed all 37 targets. This is
**not a timeout fix**. Controlled alternate-NIC testing or external wire capture
is needed to narrow the remaining cause. Cable-loss/OP-loss hardware tests were
deferred because the baseline itself was failing; no new physical-motion or
firmware-side shutdown claim is made.

## SOES candidate bring-up — 2026-09-28

The LP-AM243 was deliberately flashed with
`am243_ethercat_jw2_soes.release.appimage.hs_fs`, SHA-256
`424c7d498e35efcbdd0c5d27dc490d0c6dd8e3b81896520bc170c36a1ef10b83`.
Bootloader/application verification succeeded; after the operator selected OSPI
boot and power-cycled, the console reported `Joshua JW2 SOES ready`. Sources
were the working tree based on `7f33853`, not a clean release commit. The image
excludes TI's evaluation slave-stack library but retains TI's hardware/PRU
dependencies. Command-progress/target watchdogs were 2 s / 1 s. Motor power
was operator-confirmed disconnected; the channel remains software-only.

On Ubuntu 24.04/Jazzy Docker through `enp5s0`, discovery found one
`Joshua AM243 JW2 SOES` slave, vendor/product `0xe000059d` / `0x4a570002`,
revision `0x00020002`, 80/80-byte PDO regions and a valid `am243-soes2`
descriptor. This is a bench identity, not a registered product identity.

The first runtime handshake exposed a host CoE interoperability bug: the host
required the reply mailbox counter to equal its request counter. Capture showed
a valid reset download acknowledgment with request counter 2 and reply counter
5. SOES advances its own transmit counter independently. Removing that equality
requirement allowed reset, IDENTIFY, configuration and teardown to pass. The
production host now preserves its outgoing counter but validates replies by
CoE service, object and payload, with retained-mailbox draining, one outstanding
SDO and fault-on-timeout. Existing tests cover independent/zero reply counters,
malformed replies, aborts and owner scheduling; no board-specific host branch
or firmware change was needed.

**Hardware qualification remains incomplete.** An exercise using the temporary
counter fix failed during ENABLE on AL-state register `0x0130` (5 ms budget,
5.093 ms elapsed). A subsequent exercise with the production fix again passed
initialization, then failed during ENABLE on mailbox status `0x080d` (5 ms
budget, 5.013 ms elapsed). Passive userspace capture observed the matching WKC-1
reply approximately 5.074 ms after the request; this is not a wire-time
measurement or proof of the delay's source. Both failures preceded any
SET_TARGET. ENABLE outcome was unknown; DISABLE and teardown could not confirm
command completion after the session fault. No firmware-side stop was
independently observed. The existing timing policy was unchanged: 20 ms period,
5 ms process/state/mailbox budgets, 2 ms guard, 1 s operation/response timeouts.

A final fresh-session handshake with the production fix passed reset, identity,
configuration and teardown, confirming protocol recovery after those failures.
This does not establish watchdog timing or physical-output safety. Both full
Compose suites (`test-u22`, `test-u24`) passed all 35 targets, including the
updated EtherCAT target's 57 cases.

The older timing problem therefore persists with SOES. No SOES target/feedback,
watchdog, link-loss or continuous-over-one-hour pass is claimed, and TI-stack
retirement remains gated on those checks. Temporary probe/config/capture logs
were kept outside Git under `/tmp/joshua-soes-hw.byBEDj/`; successful flash logs
were under `/tmp/joshua-am243-soes-flash.wQ1oNh/`. No repository hardware test
utility was added. These temporary paths are historical, not durable artifacts.

### Receive-path controls and decision to defer qualification

Subsequent operator-confirmed diagnostics used the same SOES image and timing
policy, with motor power disconnected and no ENABLE or SET_TARGET commands.
The results below preserve the recorded session outcomes; the temporary probe
and capture logs were no longer present when work resumed on 2026-10-04.

- IRQ/NAPI tracing showed prompt host-worker wakeups, early receive polls and
  then a gap in NIC polling until the next transmission delivered a late reply.
  The NIC supplied no hardware receive timestamps, so exact wire arrival was
  not measured.
- Two runs using the kernel's continuous NAPI polling mode completed 500
  disabled-channel GET_FEEDBACK calls each, with successful initialization and
  teardown. Across both runs, 2222 PDO exchanges, 2110 AL-state reads and 110
  mailbox register operations succeeded. Polling consumed approximately one
  CPU core; it was restored after the experiment and was not adopted as a
  runtime policy. Returning to the original mode reproduced a 5 ms timeout.
- Disabling only IRQ deferral did not fix the stalls. A later A/B/A test with
  both `napi_defer_hard_irqs` and `gro_flush_timeout` set to zero also failed:
  both changed-setting runs failed initialization with PDO working count 1
  instead of 3. Read replies reached the kernel in about 44 microseconds;
  write replies appeared after 5.12–5.15 ms, following the next transmission.
  Original-setting controls failed AL-state reads. Capture reported no drops.
  Both settings and the polling mode were restored, and test/tracing processes
  were stopped.

The host NIC/driver interrupt path is the leading suspect, rather than a proven
specific driver defect. The operator chose to stop investigating this NIC.
Joshua retains its configured 5 ms bench budgets and has no NIC-specific polling
or timeout workaround. Hardware qualification is deferred pending a suitable
NIC. The disabled-channel diagnostic passes do not qualify SOES target handling,
watchdogs, OP/link-loss recovery or the continuous-over-one-hour retirement gate.

## Automated coverage

`serial_v2_validation_test.cc` runs these workflows against the real AM243
software handler in memory. `joshua_wire_v2_smoke_test.cc` launches the actual
CLI over an allocated Linux pseudo-terminal backed by that same handler. It
checks handshake, configuration, exercise, fresh sessions, CLI safety gates,
lost replies, wrong correlation and SIGTERM cleanup. Neither test opens real
hardware; temporary configs and output are created in Bazel's test directory.
These validate the tool, not physical board behavior or pulse timing.

Run the CLI integration test inside the matching dev container (use `u22` and
Python `3.10` for Humble):

```bash
bazel test --config=u24 --config=x86-base \
  --@rules_python//python/config_settings:python_version=3.12 \
  //robot/board/joshua_wire:joshua_wire_v2_smoke_test
```

Existing session and serial tests also cover stale/late replies and framing.
Full regression commands remain `docker compose run --rm test-u22` and `test-u24`.
