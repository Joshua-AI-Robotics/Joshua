# Teensy 4.1 STEP/DIR Bring-up Notes

## Status

- [x] PlatformIO installed
- [x] Firmware built
- [x] Firmware flashed
- [x] USB serial enumerates (`/dev/ttyACM*`)
- [x] IDENTIFY handshake verified against host `TeensyBoard::Init()` (real
      board, real firmware — `board_id=TEENSY41`, `fw_name="teensy-stepdir"`,
      `n_channels=1`, `channel_drives[0]=STEP_DIR`)
- [x] Full command path verified end to end under `launcher:joshua_main`
      with `config/config_preset/example/teensy_stepper_demo.pbtxt`: a
      `ros2 topic pub` of `10.0` (degrees) to `/teensy_stepper_1/position`
      produced 89 real STEP pulses on pin 2 (DIR on pin 3), confirmed via
      an independent `GET_FEEDBACK` query returning `position=89.0`
      (`round(10.0 * 8.888889 steps/degree) = 89`).
- ⬜ Physical stepper rotation visually confirmed with a TB6600 + motor
      wired per the diagram below — GPIO pulses are verified, motor
      response is not (no TB6600 wired for this verification pass).

Three real bugs were found and fixed getting this far, listed here since
they'll bite the next person bringing up a second board the same way:

1. `platformio.ini` had `platform = teensyduino` — the correct PlatformIO
   registry name is `platform = teensy`.
2. `joshua_wire_v1.c`'s self-include was the Bazel-style
   `#include "firmware/common/joshua_wire_v1.h"`; PlatformIO's library
   builder resolves includes relative to the library's own directory, so
   it must be `#include "joshua_wire_v1.h"`.
3. `firmware/common/library.json` needed a `srcFilter` excluding
   `*_test.cc` — without it PlatformIO tries to compile the host-only
   gtest file into the firmware image.
4. Separately, `node_generator/node_generator.cc`'s
   `IsCppDriverAvailableForAction` didn't list `MOTOR_STEPPER_NEMA17`, so
   the launcher silently routed the actuator to the Python backend
   (`action_factory.py`, which doesn't know the motor type) instead of the
   C++ one this PR built. Fixed by adding the case.
5. On Linux, flashing needs `/etc/udev/rules.d/00-teensy.rules` (from
   pjrc.com) installed, and `upload_protocol = teensy-cli` set in
   `platformio.ini` — the default `teensy-gui` tries to launch a GUI
   Teensy Loader app that isn't installed on a headless dev machine.

This firmware and its host counterpart (`robot/board/teensy/teensy_board.*`)
are now verified both by host-side unit tests
(`bazel test //robot/board/teensy:teensy_board_test
//firmware/common:joshua_wire_v1_test`), which exercise the exact wire
bytes this firmware encodes/decodes but not the MCU itself.

## Hardware

- TI/PJRC Teensy 4.1
- TB6600 stepper drive
- NEMA17 stepper motor
- Bench power supply for the TB6600 (do not power the stepper from the
  Teensy's 5V/3.3V rails)

## Wiring (matches `src/channel_table.c`, channel 0)

```
Teensy 4.1 pin 2  ──► TB6600 PUL+   (STEP)
Teensy 4.1 pin 3  ──► TB6600 DIR+   (DIR)
Teensy 4.1 pin 4  ──► TB6600 ENA+   (ENABLE)
Teensy 4.1 GND    ──► TB6600 PUL-, DIR-, ENA-  (common ground — required)
```

TB6600 motor-side (A+/A-/B+/B-) wiring depends on your specific NEMA17's
coil pinout; consult the motor's datasheet. TB6600 power-side (VCC/GND) is
the bench supply, sized for your motor's rated current — set the TB6600's
current-limit DIP switches to match the motor before powering it, not
after.

To add a second channel, add an entry to `g_channels[]` in
`src/channel_table.c` with a free set of pins, then declare a second
`channels { index: 1 ... }` block in the host config. No other firmware or
host code changes needed.

## Install PlatformIO

```bash
python3 -m pip install --user platformio
# or: pipx install platformio
pio --version
```

## Build

```bash
cd firmware/teensy/41
pio run
```

This pulls `firmware/common/joshua_wire_v1.{h,c}` in via `platformio.ini`'s
`symlink://../../common` and compiles it alongside `src/*.cpp`/`*.c` for
the `teensy41-serial` environment.

## Flash

On Linux, one-time setup so a non-root user can write to the Teensy's USB
bootloader device:

```bash
cd /tmp
wget https://www.pjrc.com/teensy/00-teensy.rules
sudo cp 00-teensy.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

`platformio.ini` already sets `upload_protocol = teensy-cli` — the default
`teensy-gui` launches a GUI Teensy Loader app that isn't installed on a
headless machine.

Plug the Teensy in over USB (a brand-new/blank board boots straight into
HalfKay bootloader mode — `lsusb` shows "Teensy Halfkay Bootloader" — which
is expected and exactly what needs flashing), then:

```bash
pio run --target upload
```

No boot-mode DIP switches or separate debug probe needed — Teensy reflashes
over the same USB port it runs on. After a successful flash it reboots into
the application and enumerates as a CDC serial device (`/dev/ttyACM*`,
`lsusb` now shows "Teensyduino Serial").

## Verify the board enumerates

```bash
ls /dev/ttyACM*
```

The Teensy's native USB shows up as a CDC serial device, same class as the
XDS110 debug probes elsewhere in this repo — should appear within a couple
seconds of flashing or power-up.

## Verify the wire protocol against real firmware

Once flashed, the cleanest first check is the host's own `TeensyBoard::Init()`
path via the launcher with the example preset (adjust `port` in the preset
to match your `/dev/ttyACM*`):

```bash
bazel run //launcher:joshua_main -- \
  --config=config/config_preset/example/teensy_stepper_demo.pbtxt
```

`TeensyBoard::Init()` runs IDENTIFY (checks `fw_name == "teensy-stepdir"`,
protocol version, and that channel 0 reports `STEP_DIR`) then pushes
`CONFIGURE_CHANNEL`. A mismatch here (wrong image flashed, wrong port, bad
wiring) fails at `Init()` with an actionable error — see
`robot/board/teensy/teensy_board.cc`'s `IdentifyAndValidate`. **Verified
against real hardware** — look for `StepperDriver actuator ID: ... initialized`
in the log with no preceding `ERROR`.

Then drive it:

```bash
source /opt/ros/humble/setup.bash   # or your ROS 2 distro
ros2 topic pub --once /teensy_stepper_1/position std_msgs/msg/Float32 "data: 10.0"
```

**Verified against real hardware** — this produced 89 real STEP pulses
(pin 2, DIR on pin 3), confirmed both by no `ERROR` in the launcher log and
by independently querying `GET_FEEDBACK` over the wire and reading back
`position=89.0` steps.

## Known gaps to close before trusting this near real hardware

- No acceleration ramp — `backend_stepdir.cpp` steps at a constant rate
  capped by `max_pulse_rate_hz`; starting/stopping abruptly at a high rate
  can stall or skid a real stepper. Tune `max_pulse_rate_hz` down first,
  add ramping once there's a real motor to tune it against.
- `GET_FEEDBACK`'s `velocity` field is the last commanded target when in
  velocity mode, not a measurement — this firmware has no encoder.
- Serial read timeouts (`kByteTimeoutMs` in `transport_serial.cpp`, the
  `AtomicRead` timeouts in `robot/comm/serial/serial.cc`) worked at
  115200 baud over real USB for IDENTIFY/CONFIGURE_CHANNEL/SET_TARGET/
  GET_FEEDBACK round trips in this verification pass; not stress-tested
  under load or at higher command rates.
- No TB6600 was wired for this verification pass — GPIO pulses are
  confirmed via `GET_FEEDBACK`, actual motor rotation is not yet observed.
