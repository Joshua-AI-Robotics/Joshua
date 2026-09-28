# JoshuaWire v2 serial validation

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
