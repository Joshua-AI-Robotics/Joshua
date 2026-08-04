# Teensy 4.1 STEP/DIR Bring-up Notes

## Status

- ⬜ PlatformIO installed
- ⬜ Firmware built
- ⬜ Firmware flashed
- ⬜ USB serial enumerates (`/dev/ttyACM*`)
- ⬜ IDENTIFY handshake verified against host `TeensyBoard::Init()`
- ⬜ Stepper moves under `launcher:joshua_main` with
  `config/config_preset/example/teensy_stepper_demo.pbtxt`

Nothing below this line has been run against real hardware yet — this
firmware and its host counterpart (`robot/board/teensy/teensy_board.*`) are
verified today only by host-side unit tests
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

Plug the Teensy in over USB, then:

```bash
pio run --target upload
```

PlatformIO's Teensy platform drives `teensy_loader_cli` (or prompts you to
press the Teensy's physical reset button on first flash — the bootloader
button on the board, not a software step) to upload over USB. No boot-mode
DIP switches or separate debug probe needed — Teensy reflashes over the
same USB port it runs on.

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
`robot/board/teensy/teensy_board.cc`'s `IdentifyAndValidate`.

## Known gaps to close before trusting this near real hardware

- No acceleration ramp — `backend_stepdir.cpp` steps at a constant rate
  capped by `max_pulse_rate_hz`; starting/stopping abruptly at a high rate
  can stall or skid a real stepper. Tune `max_pulse_rate_hz` down first,
  add ramping once there's a real motor to tune it against.
- `GET_FEEDBACK`'s `velocity` field is the last commanded target when in
  velocity mode, not a measurement — this firmware has no encoder.
- Serial read timeouts (`kByteTimeoutMs` in `transport_serial.cpp`, the
  AtomicRead timeouts in `robot/comm/serial/serial.cc`) are unverified
  guesses; may need tuning once real USB round-trip latency is measured.
