# Teensy 4.1 — joshua_wire_v1 STEP/DIR firmware

Joshua-owned firmware (not a vendor demo) for a Teensy 4.1 driving STEP/DIR
channels — a TB6600 in the reference wiring, but this firmware only ever
toggles STEP/DIR/ENA pins; it never names the stepper drive chip
(docs/BOARD_LAYER_RFC.md §5.2). Speaks `joshua_wire_v1` over native USB
serial. Paired host-side class: `robot/board/teensy/teensy_board.*`.

## Layout

```text
firmware/teensy/41/
  platformio.ini        one env per wiring variant (today: teensy41-serial)
  src/
    main.cpp             setup()/loop(), command dispatch
    channel_table.c       THE pinout contract (docs/BOARD_LAYER_RFC.md §7.5)
    channel_table.h
    backend_stepdir.{h,cpp}   STEP/DIR/ENA pulse generation
    transport_serial.{h,cpp} joshua_wire_v1 framing over Serial
  docs/bringup.md        detailed bring-up log: full wiring diagram,
                         verification trace, bugs found and fixed
```

`joshua_wire_v1.{h,c}` itself is not copied here — `platformio.ini` pulls it
in directly from `firmware/common/` via `lib_deps = symlink://../../common`,
so this firmware and the host (`//firmware/common:joshua_wire_v1` in Bazel)
always build from the exact same two files.

## Status

- [x] Toolchain installed (PlatformIO via `pipx`)
- [x] Firmware built (`pio run`)
- [x] Firmware flashed (`pio run --target upload`)
- [x] Board enumerates (`/dev/ttyACM0`, `lsusb` shows "Teensyduino Serial")
- [x] IDENTIFY handshake verified against real hardware — returned
      `board_id=TEENSY41`, `fw_name="teensy-stepdir"`, `n_channels=1`,
      `channel_drives[0]=STEP_DIR`
- [x] Full command path verified end to end — a `ros2 topic pub` of 10
      degrees produced 89 real STEP pulses, confirmed via an independent
      `GET_FEEDBACK` query returning `position=89.0`
- [x] **Motor physically rotates**, wired to a real TB6600, driven through
      the real production path (`launcher:joshua_main` +
      `ros2 topic pub`). Getting here took a real debugging session —
      the wiring, DIP switch, and driver-behavior bugs it surfaced are
      documented in full in `docs/bringup.md`, worth reading before
      bringing up a second board.

## Prerequisites

- Hardware: Teensy 4.1, a USB-C (or micro-USB, depending on your Teensy
  4.1 revision) cable, and — for the final wiring/motion step — a TB6600
  stepper drive, a NEMA17 (or similar STEP/DIR-compatible) motor, and a
  bench power supply for the TB6600.
- OS packages: none beyond Python 3 + pip (used to install PlatformIO).
- Toolchain: [PlatformIO Core](https://platformio.org/) — no separate
  vendor SDK or IDE required; PlatformIO downloads the Teensy platform,
  toolchain, and Arduino framework on first build.
- Accounts: none.

## Install

```bash
pipx install platformio
# or: python3 -m pip install --user platformio
pio --version
```

On Linux, also install the udev rules so a non-root user can flash over
USB (one-time, needs sudo):

```bash
cd /tmp
wget https://www.pjrc.com/teensy/00-teensy.rules
sudo cp 00-teensy.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

## Build

```bash
cd firmware/teensy/41
pio run
```

This pulls `firmware/common/joshua_wire_v1.{h,c}` in via the `symlink://`
`lib_deps` entry and compiles it alongside `src/*.cpp`/`*.c` for the
`teensy41-serial` environment. First run also downloads the `teensy`
platform, ARM toolchain, and Arduino framework (a few hundred MB).

## Flash

Plug the Teensy in over USB. A brand-new/blank board boots straight into
HalfKay bootloader mode (`lsusb` shows "Teensy Halfkay Bootloader") — that
is expected and exactly what needs flashing.

```bash
pio run --target upload
```

`platformio.ini` sets `upload_protocol = teensy-cli` so this drives the
command-line `teensy_loader_cli` rather than the GUI Teensy Loader app
(which isn't installed on a headless machine). After a successful flash
the board reboots into the application and enumerates as a CDC serial
device (`/dev/ttyACM*`; `lsusb` now shows "Teensyduino Serial").

## Verify

```bash
ls /dev/ttyACM*                        # 1. board enumerates
pio device monitor -b 115200           # optional: watch for crashes/prints
```

Then the protocol-level handshake, via the actual host code (adjust `port`
in the preset to match your `/dev/ttyACM*` first):

```bash
bazel run //launcher:joshua_main -- \
  --config=config/config_preset/example/teensy_stepper_demo.pbtxt
```

Look for `StepperDriver actuator ID: ... initialized` in the log with no
preceding `ERROR` — that means `TeensyBoard::Init()`'s IDENTIFY handshake
and `CONFIGURE_CHANNEL` push both succeeded against the real board. Then
drive it:

```bash
source /opt/ros/humble/setup.bash   # or your ROS 2 distro
ros2 topic pub --once /teensy_stepper_1/position std_msgs/msg/Float32 "data: 10.0"
```

No error in the launcher log means the command reached the firmware. See
`docs/bringup.md` for how to independently confirm this with a raw
`GET_FEEDBACK` query (bypassing the host stack entirely) if you want to
verify the wire protocol itself, not just "no error."

## Wiring / Pinout

The pinout contract lives in `src/channel_table.c`, not here — see that
file for the current channel 0 wiring (STEP/DIR/ENABLE pins) and
`docs/bringup.md` for the full wiring diagram against a TB6600, including
the ground-return connection (`ENA-`/`PUL-`/`DIR-` to Teensy GND) that
caused the most debugging time when it was missing — read that section
before wiring a second board the same way.

## Known gaps / Troubleshooting

- No acceleration ramp — `backend_stepdir.cpp` steps at a constant rate
  capped by `max_pulse_rate_hz`; tune that value down before attaching a
  real motor, since an abrupt start/stop at a high rate can stall or skid
  a stepper. Ramping is unstarted follow-up work.
- `max_pulse_rate_hz` (move speed) is a boot-time-only config value, not
  adjustable per-command through the position topic — see "Tuning
  `max_pulse_rate_hz`" in `docs/bringup.md`.
- `GET_FEEDBACK`'s `velocity` field is the last commanded target when in
  velocity mode, not a measurement — this firmware has no encoder.
- If `pio run` fails with `UnknownPackageError` for `teensyduino`, the
  platform name in `platformio.ini` regressed — it must be `platform =
  teensy` (registry name), not `teensyduino`.
- If upload hangs looking for "Teensy Loader" (a GUI app), check
  `upload_protocol = teensy-cli` is still set in `platformio.ini`.
- If commands succeed (no `ERROR` in the launcher log) but the motor
  doesn't respond: (1) check for a duplicate `joshua_main`/
  `actuator_subscriber` process fighting over the port
  (`ps aux | grep -E "joshua_main|actuator_subscriber" | grep -v grep`
  should show exactly one of each), (2) check `ENA-`/`PUL-`/`DIR-` are
  actually wired to Teensy GND, not floating. Both cost real debugging
  time on the first bring-up — full story in `docs/bringup.md`.
- Full troubleshooting/bug list from the first real hardware bring-up is
  in `docs/bringup.md`.

## Related files

- `robot/board/teensy/teensy_board.*` — paired host-side `BoardInterface`
- `robot/board/teensy/teensy_driver_smoke.cc` — board-level smoke test,
  bypasses ActionFactory/ROS entirely (`bazel run
  //robot/board/teensy:teensy_driver_smoke -- /dev/ttyACM0`)
- `robot/action/motors/drivers/stepper_driver.*` — paired motor driver
- `firmware/common/joshua_wire_v1.{h,c}` — the shared wire codec
- `config/config_preset/example/teensy_stepper_demo.pbtxt` — example preset
- `docs/bringup.md` — detailed wiring diagram, verification trace, and the
  bug list from the first real hardware bring-up
