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
hash, board, timing, power state, commands and results. Keep bench records,
temporary programs, configurations and captures outside the repository.

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
