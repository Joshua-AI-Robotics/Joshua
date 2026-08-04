# Teensy 4.1 STEP/DIR Bring-up Log

For "how do I flash this," see `../README.md` — it follows the repo's
`firmware/FLASHING_TEMPLATE.md` structure and is the up-to-date how-to.
This file is the detailed bring-up record: the full wiring diagram, the
exact verification trace from the first real hardware run, the bugs that
run surfaced, and open gaps — kept here rather than in the README so the
README stays a clean how-to and this stays the historical/detailed record.

## Status

- [x] PlatformIO installed, firmware built and flashed, board enumerates
- [x] IDENTIFY handshake verified against host `TeensyBoard::Init()` (real
      board, real firmware — `board_id=TEENSY41`, `fw_name="teensy-stepdir"`,
      `n_channels=1`, `channel_drives[0]=STEP_DIR`)
- [x] Full command path verified end to end under `launcher:joshua_main`
      with `config/config_preset/example/teensy_stepper_demo.pbtxt`: a
      `ros2 topic pub` of `10.0` (degrees) to `/teensy_stepper_1/position`
      produced 89 real STEP pulses on pin 2 (DIR on pin 3), confirmed via
      an independent `GET_FEEDBACK` query returning `position=89.0`
      (`round(10.0 * 8.888889 steps/degree) = 89`)
- ⬜ Physical stepper rotation visually confirmed with a TB6600 + motor
      wired per the diagram below — GPIO pulses are verified, motor
      response is not (no TB6600 wired for this verification pass)

## Bugs found and fixed getting this far

Real hardware surfaced bugs a software-only pass (unit tests, golden
bytes) couldn't catch. Listed here since they'll bite the next person
bringing up a second board the same way:

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

## Verifying the raw wire protocol (bypassing the host stack)

Useful for isolating "is it the firmware/wire protocol" from "is it the
host C++ stack" when debugging. Independently confirms the frame codec
against real firmware without going through `TeensyBoard`/Bazel at all:

```python
import serial, time, struct

def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc

def build_frame(cmd, channel, payload=b""):
    body = bytes([1, cmd, channel]) + payload
    crc = crc16_ccitt_false(body)
    return bytes([0xA5, len(body)]) + body + crc.to_bytes(2, 'little')

port = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
time.sleep(0.3)
port.reset_input_buffer()

# GET_FEEDBACK, channel 0 (cmd=0x04)
port.write(build_frame(0x04, 0))
resp = port.read(17)  # JW1_FRAME_LEN(JW1_FEEDBACK_RESPONSE_PAYLOAD_LEN) = 17
position, velocity = struct.unpack('<ff', resp[5:13])
fault_flags = struct.unpack('<H', resp[13:15])[0]
print(f"position={position} steps, velocity={velocity}, fault_flags={fault_flags}")
```

This is exactly how the "89 real STEP pulses" result in Status above was
independently confirmed, separate from the launcher's log output.

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
