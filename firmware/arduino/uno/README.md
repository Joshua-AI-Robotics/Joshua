# Arduino Uno R3 — joshua_wire_v1 STEP/DIR firmware

Joshua-owned firmware (not a vendor sketch) for an Arduino Uno R3
(ATmega328P) driving STEP/DIR channels — a TB6600 in the reference wiring,
but this firmware only ever toggles STEP/DIR/ENA pins; it never names the
stepper drive chip (docs/BOARD_LAYER_RFC.md §5.2). Speaks `joshua_wire_v1`
over the Uno's USB-serial UART at 115200 baud. Paired host-side class:
`robot/board/arduino/arduino_board.h`.

This image is for the **Uno R3 / ATmega328P** (`BoardType::ARDUINO_UNO`).
A different AVR (Mega, Leonardo) would be a new firmware directory, not a
runtime switch.

## Layout

```text
firmware/arduino/uno/
  platformio.ini        one env per wiring variant (today: uno-serial);
                        -I src in build_flags so firmware/common/
                        libraries can see this project's channel_table.h
  src/
    main.cpp             setup()/loop(), command dispatch
    channel_table.c       channel *count* per firmware image (compile-time);
                          pin numbers are host-configured, not here
    channel_table.h
    transport_serial.{h,cpp} joshua_wire_v1 framing over UART Serial
```

`joshua_wire_v1.{h,c}` and `backend_stepdir.{h,cpp}` are not copied here —
`platformio.ini` pulls them from `firmware/common/` via
`lib_deps = symlink://../../common` (docs/BOARD_LAYER_RFC.md §7.3).

## Status

Host software and this firmware tree are in place. None of the hardware
boxes are checked — they wait on a real Uno R3 on the author's desk.

- [ ] Toolchain installed
- [ ] Firmware built (`pio run`)
- [ ] Firmware flashed (`pio run --target upload`)
- [ ] Board enumerates (`/dev/ttyACM0` or `/dev/ttyUSB0`)
- [ ] IDENTIFY handshake verified against the host (current goal: USB only)
- [ ] Full command path verified end to end (deferred until a drive is wired)
- [ ] Motor physically rotates (deferred)

## Prerequisites

- Hardware: Arduino Uno R3 (or 328P-compatible) and a USB cable for
  IDENTIFY. A TB6600, NEMA17, and bench PSU are only needed later for
  motion — **not** this pass, and never the Uno 5V rail.
- OS packages: none beyond Python 3 + pip (used to install PlatformIO).
  The uploading user must be in the `dialout` group on Linux.
- Toolchain: [PlatformIO Core](https://platformio.org/). First build
  downloads `atmelavr` + avr-gcc + Arduino AVR core.
- Accounts: none.

## Install

```bash
pipx install platformio
# or: python3 -m pip install --user platformio
pio --version
sudo usermod -aG dialout "$USER"   # Linux; log out and back in
```

## Build

```bash
cd firmware/arduino/uno
pio run
```

## Flash

Plug the Uno in over USB. Official R3 boards usually appear as
`/dev/ttyACM*`; CH340 clones as `/dev/ttyUSB*`.

```bash
pio run --target upload
# if PlatformIO picked the wrong port:
pio run --target upload --upload-port /dev/ttyUSB0
```

This uses avrdude through the stock bootloader (DTR reset). After a
successful flash the sketch runs immediately; opening the port from the
host resets the 328P again — `ArduinoBoard::CreateTransport()` waits 2s
for that reboot before IDENTIFY.

## Verify (IDENTIFY-only)

USB cable only — no TB6600, no motor. Close any serial monitor first so
the host can open the port.

```bash
ls /dev/ttyACM* /dev/ttyUSB*       # board enumerates
# do not leave `pio device monitor` running; it holds the port
```

Then the handshake via host code (no ROS, no motion commands):

```bash
bazel run //robot/board/arduino:arduino_driver_smoke -- /dev/ttyACM0
# CH340 clone:  ... -- /dev/ttyUSB0
```

Success is `Init: OK` plus `IDENTIFY + CONFIGURE_CHANNEL succeeded`. That
means the board reported `board_id=ARDUINO_UNO`, protocol v1, one STEP_DIR
channel, and accepted pin config. `CONFIGURE_CHANNEL` only sets pin modes
on 2/3/4; with nothing wired that is harmless.

Optional: launcher Init (also IDENTIFY; do not `ros2 topic pub` yet):

```bash
bazel run //launcher:joshua_main -- \
  --config=config/config_preset/example/arduino_stepper_demo.pbtxt
```

Look for `StepperDriver actuator ID: ... initialized` with no `ERROR`.

Motion (`--move` on the smoke binary, or a position topic) is deferred
until a drive is wired.

## Wiring / Pinout (not needed for IDENTIFY)

Pin numbers are host-configured (`StepDirConfig` in the pbtxt), not
hardcoded in `channel_table.c`. Do **not** wire STEP/DIR/ENA to pins 0 or
1 — those are the UART to the USB-serial chip.

Later, TB6600 reference (matches
`config/config_preset/example/arduino_stepper_demo.pbtxt` channel 0). The
Uno is 5V; keep `PUL-`/`DIR-`/`ENA-` tied to Uno GND.

```
Uno pin 2  ──► TB6600 PUL+   (STEP)
Uno pin 3  ──► TB6600 DIR+   (DIR)
Uno pin 4  ──► TB6600 ENA+   (ENABLE)
Uno GND    ──► TB6600 PUL-, DIR-, ENA-  (ALL THREE)

TB6600 A+/A-, B+/B- ──► motor coil pairs
TB6600 VCC/GND      ──► bench PSU; set current-limit DIP switches before
                        powering the motor. Do not power the motor from
                        the Uno 5V pin.
```

## Known gaps / Troubleshooting

- First hardware pass is IDENTIFY-only (USB). Do not attach a motor until
  that handshake is green.
- Hardware boxes above stay unchecked until someone runs them on a real
  Uno.
- 16 MHz AVR + `digitalWrite` cannot match Teensy pulse rates. Start at
  `max_pulse_rate_hz: 1000` and raise only until the motor stalls.
- Opening the serial port resets the board (16U2 DTR). If IDENTIFY fails
  immediately, confirm `ArduinoBoard::CreateTransport()` still sleeps 2s
  after open, and that nothing else (a serial monitor) is holding the
  port.
- No acceleration ramp — same `backend_stepdir` limit as Teensy.
- `GET_FEEDBACK` velocity is last commanded target, not a measurement.
- **TODO: no build/flash version identifier.** Same gap as Teensy
  (`docs/BOARD_LAYER_RFC.md` §12 item 9).

## Related files

- `robot/board/arduino/arduino_board.h` — host class; identity plus DTR settle
- `robot/board/joshua_wire/joshua_wire_board.*` — shared IDENTIFY / dispatch
- `robot/board/arduino/arduino_driver_smoke.cc` — IDENTIFY-only by default;
  pass `--move` only after a drive is wired
- `robot/action/motors/drivers/stepper_driver.*` — paired motor driver
- `firmware/common/joshua_wire_v1.{h,c}` — shared wire codec
- `firmware/teensy/41/` — the worked example this image was copied from
- `config/config_preset/example/arduino_stepper_demo.pbtxt` — example preset
