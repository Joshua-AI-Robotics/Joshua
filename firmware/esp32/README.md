# ESP32 — joshua_wire_v1 STEP/DIR firmware

Joshua-owned firmware (not a vendor demo) for an ESP32 driving STEP/DIR
channels — a TB6600 in the reference wiring, but this firmware only ever
toggles STEP/DIR/ENA pins; it never names the stepper drive chip
(docs/BOARD_LAYER_RFC.md §5.2). Speaks `joshua_wire_v1` over UART/USB-serial
— the same wire protocol and shared drive backend as `firmware/teensy/41/`,
not the Wi-Fi/UDP transport variant `docs/BOARD_LAYER_RFC.md` originally
speculated ESP32 might prove (that needs a `robot::comm::UdpTransport` that
doesn't exist yet — see `robot/board/frame/frame_transport.h`'s TODO).
Paired host-side class: `robot/board/esp32/esp32_board.h` (header-only).

## Layout

```text
firmware/esp32/
  platformio.ini        one env (esp32-serial); board = esp32dev by
                        default — change this one line for a different
                        ESP32 variant (S3, C3, S2, ...); -I src so
                        firmware/common/ libraries can see this project's
                        own channel_table.h
  src/
    main.cpp             setup()/loop(), command dispatch — identical to
                          firmware/teensy/41/'s except HandleIdentify()'s
                          board_id
    channel_table.c       channel *count* per firmware image (compile-time);
                          pin numbers are host-configured, not here — see
                          Wiring / Pinout below (docs/BOARD_LAYER_RFC.md §7.5)
    channel_table.h
    transport_serial.{h,cpp} joshua_wire_v1 framing over Serial
```

`joshua_wire_v1.{h,c}` and `backend_stepdir.{h,cpp}` are not copied here —
`platformio.ini` pulls them in directly from `firmware/common/` via
`lib_deps = symlink://../common` (one `..` shorter than Teensy's
`../../common`, since this directory has no chip-revision subdirectory) —
same reasoning as `firmware/teensy/41/README.md`: `joshua_wire_v1` because
host and firmware must agree on the wire format for a given commit;
`backend_stepdir` because STEP/DIR/ENA pulse generation is a fact about the
driver chip, not the MCU — the same `digitalWrite`-based source already
proven on Teensy works unchanged here, no ESP32-specific code needed.

## Status

- [x] Toolchain installed (PlatformIO via `pipx`)
- [x] Firmware built (`pio run`) — clean build, all of `firmware/common/`
      reused unchanged
- ⬜ Firmware flashed
- ⬜ Board enumerates
- ⬜ Protocol/handshake verified against the host
- ⬜ Full command path verified end to end

## Prerequisites

- Hardware: an ESP32 dev board (this firmware targets a generic
  `esp32dev`-class board by default — see `platformio.ini` if yours is a
  different variant), a USB cable, and — for the wiring/motion step — a
  TB6600 stepper drive, a NEMA17 (or similar STEP/DIR-compatible) motor,
  and a bench power supply for the TB6600.
- OS packages: none beyond Python 3 + pip (used to install PlatformIO).
- Toolchain: [PlatformIO Core](https://platformio.org/) — no separate
  vendor SDK or IDE required; PlatformIO downloads the `espressif32`
  platform, Xtensa toolchain, and Arduino framework on first build.
- Accounts: none.

## Install

```bash
pipx install platformio
# or: python3 -m pip install --user platformio
pio --version
```

On Linux, a non-root user typically needs to be in the `dialout` group to
access the USB-serial device (the CP2102/CH340/... bridge chip most ESP32
boards use, distinct from Teensy's native-USB udev rule):

```bash
sudo usermod -a -G dialout $USER
# log out and back in for the group change to take effect
```

## Build

```bash
cd firmware/esp32
pio run
```

This pulls `firmware/common/joshua_wire_v1.{h,c}` and
`firmware/common/backend_stepdir.{h,cpp}` in via the `symlink://` `lib_deps`
entry and compiles them alongside `src/*.cpp`/`*.c` for the `esp32-serial`
environment. First run also downloads the `espressif32` platform, Xtensa
toolchain, and Arduino framework (several hundred MB).

## Flash

Plug the ESP32 in over USB.

```bash
pio run --target upload
```

PlatformIO auto-detects the serial port and drives `esptool.py`
(bundled with the `espressif32` platform) — no special
`upload_protocol` override needed, unlike Teensy's `teensy-cli`. Some
boards need the BOOT button held during the "Connecting..." phase if
auto-reset into the bootloader doesn't work over your particular
USB-serial bridge; check `pio run --target upload -v` output if the
upload hangs at that point.

## Verify

```bash
ls /dev/ttyUSB*                        # 1. board enumerates (or /dev/ttyACM*
                                        #    on native-USB variants like S2/S3)
pio device monitor -b 115200           # optional: watch for crashes/prints
```

Then the protocol-level handshake, via the actual host code (adjust `port`
in the preset to match your `/dev/ttyUSB*` first):

```bash
bazel run //launcher:joshua_main -- \
  --config=config/config_preset/example/esp32_stepper_demo.pbtxt
```

Look for `StepperDriver actuator ID: ... initialized` in the log with no
preceding `ERROR` — that means `Esp32Board::Init()`'s IDENTIFY handshake
and `CONFIGURE_CHANNEL` push both succeeded against the real board. Then
drive it:

```bash
source /opt/ros/humble/setup.bash   # or your ROS 2 distro
ros2 topic pub --once /esp32_stepper_1/position std_msgs/msg/Float32 "data: 10.0"
```

No error in the launcher log means the command reached the firmware. To
verify the wire protocol itself independent of the host C++ stack, use
`esp32_driver_smoke`:

```bash
bazel run //robot/board/esp32:esp32_driver_smoke -- /dev/ttyUSB0
```

## Wiring / Pinout

**Pin numbers are host-configured** (`StepDirConfig.step_pin`/`dir_pin`/
`enable_pin` in the pbtxt, pushed to firmware via `CONFIGURE_CHANNEL` at
`Init()`), not hardcoded in `channel_table.c`
(`docs/BOARD_LAYER_RFC.md` §7.5, revised). `channel_table.c` only declares
how many channel *slots* this firmware image has — still compile-time,
since that's reported by `IDENTIFY`.

`config/config_preset/example/esp32_stepper_demo.pbtxt` uses GPIO 25
(STEP), 26 (DIR), 27 (ENABLE) — ordinary general-purpose output pins on a
standard ESP32 DevKitC-style board: not strapping pins (0, 2, 5, 12, 15),
not input-only (34–39), not the default UART0/flash pins. A reasonable
starting choice, not a requirement — rewire and edit the pbtxt freely.

Reference wiring against a TB6600 (same shape as
`firmware/teensy/41/README.md`'s, different pin numbers):

```
ESP32 GPIO 25  ──► TB6600 PUL+   (STEP)
ESP32 GPIO 26  ──► TB6600 DIR+   (DIR)
ESP32 GPIO 27  ──► TB6600 ENA+   (ENABLE)
ESP32 GND      ──► TB6600 PUL-, DIR-, ENA-  (ALL THREE)

TB6600 A+/A-, B+/B- ──► motor coil pairs (get these from the motor's
                        datasheet or a multimeter continuity check, not
                        wire color — conventions vary by manufacturer)
TB6600 VCC/GND      ──► bench PSU, sized to the motor's rated current;
                        set the TB6600's current-limit DIP switches to
                        match BEFORE powering on. Do not power the motor
                        from the ESP32's 5V/3.3V rails, and do not tie
                        ESP32 GND to the PSU's power GND — only the
                        signal-side ground (PUL-/DIR-/ENA-) ties to the
                        ESP32; that separation is the point of the
                        opto-isolated inputs.
```

**Not yet confirmed against real hardware** — this wiring hasn't had the
Teensy bring-up's actual verification pass yet (see Status above). The
`PUL-`/`DIR-`/`ENA-` ground-return requirement and the DIP-switch caveat
that cost the most debugging time on the Teensy bring-up
(`firmware/teensy/41/README.md`'s Wiring / Pinout section) almost
certainly apply here too, since they're facts about the TB6600, not the
MCU — but treat that as a prediction until it's actually been run.

## Known gaps / Troubleshooting

- Not yet flashed or run against real hardware — everything above the
  "Verify" section is unverified prediction, carried over from Teensy's
  bring-up rather than confirmed on this board. Update this section (and
  Status above) once it has been.
- `board = esp32dev` in `platformio.ini` targets a generic ESP32 DevKitC-
  class board. If your board is a different variant (S3, C3, S2, an
  ESP32-WROVER module, ...), that one line likely needs to change —
  `pio boards espressif32` lists what PlatformIO knows about.
- No acceleration ramp — `backend_stepdir.cpp` (shared with Teensy) steps
  at a constant rate capped by `max_pulse_rate_hz`; tune that value down
  before attaching a real motor. Ramping is unstarted follow-up work.
- `max_pulse_rate_hz` (move speed) is a boot-time-only config value, not
  adjustable per-command through the position topic.
- `GET_FEEDBACK`'s `velocity` field is the last commanded target when in
  velocity mode, not a measurement — this firmware has no encoder.

## Related files

- `robot/board/esp32/esp32_board.h` — paired host-side board class;
  header-only, a one-line constructor supplying `BoardType::ESP32` and
  `JW1_BOARD_ESP32` to `JoshuaWireBoard` — everything else is inherited
- `robot/board/joshua_wire/joshua_wire_board.*` — the shared IDENTIFY
  handshake, `CONFIGURE_CHANNEL` push, and channel dispatch every
  joshua_wire_v1 host board (Teensy, ESP32, Arduino later) runs through
  unchanged (docs/BOARD_LAYER_RFC.md §7.3)
- `robot/board/esp32/esp32_driver_smoke.cc` — board-level smoke test,
  bypasses ActionFactory/ROS entirely (`bazel run
  //robot/board/esp32:esp32_driver_smoke -- /dev/ttyUSB0`)
- `robot/action/motors/drivers/stepper_driver.*` — paired motor driver
- `firmware/common/joshua_wire_v1.{h,c}` — the shared wire codec
- `firmware/common/backend_stepdir.{h,cpp}` — the shared STEP/DIR/ENA
  drive backend, reused as-is
- `config/config_preset/example/esp32_stepper_demo.pbtxt` — example preset
- `firmware/teensy/41/README.md` — the worked example this board mirrors,
  including the fuller debugging notes from that board's first real
  hardware pass
