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
    channel_table.c       channel *count* per firmware image (compile-time);
                          pin numbers are host-configured, not here — see
                          Wiring / Pinout below (docs/BOARD_LAYER_RFC.md §7.5)
    channel_table.h
    backend_stepdir.{h,cpp}   STEP/DIR/ENA pulse generation
    transport_serial.{h,cpp} joshua_wire_v1 framing over Serial
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
      `board_id=TEENSY41`, `n_channels=1`, `channel_drives[0]=STEP_DIR`
      (`fw_name` is sent zeroed; no host code reads it — see Wiring /
      Pinout below and `docs/BOARD_LAYER_RFC.md` §7.5)
- [x] Full command path verified end to end — a `ros2 topic pub` of 10
      degrees produced 89 real STEP pulses, confirmed via an independent
      `GET_FEEDBACK` query returning `position=89.0`
- [x] **Motor physically rotates**, wired to a real TB6600, driven through
      the real production path (`launcher:joshua_main` +
      `ros2 topic pub`).

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

No error in the launcher log means the command reached the firmware. To
verify the wire protocol itself independent of the host C++ stack, use
`teensy_driver_smoke` (see Related files below) rather than the launcher.

## Wiring / Pinout

**Pin numbers are host-configured** (`StepDirConfig.step_pin`/`dir_pin`/
`enable_pin` in the pbtxt, pushed to firmware via `CONFIGURE_CHANNEL` at
`Init()`), not hardcoded in `channel_table.c`
(`docs/BOARD_LAYER_RFC.md` §7.5, revised). `channel_table.c` only declares
how many channel *slots* this firmware image has — still compile-time,
since that's reported by `IDENTIFY`.

Reference wiring against a TB6600 (matches
`config/config_preset/example/teensy_stepper_demo.pbtxt`'s channel 0):

```
Teensy 4.1 pin 2  ──► TB6600 PUL+   (STEP)
Teensy 4.1 pin 3  ──► TB6600 DIR+   (DIR)
Teensy 4.1 pin 4  ──► TB6600 ENA+   (ENABLE)
Teensy 4.1 GND    ──► TB6600 PUL-, DIR-, ENA-  (ALL THREE)

TB6600 A+/A-, B+/B- ──► motor coil pairs (get these from the motor's
                        datasheet or a multimeter continuity check, not
                        wire color — conventions vary by manufacturer)
TB6600 VCC/GND      ──► bench PSU, sized to the motor's rated current;
                        set the TB6600's current-limit DIP switches to
                        match BEFORE powering on. Do not power the motor
                        from the Teensy's 5V/3.3V rails, and do not tie
                        Teensy GND to the PSU's power GND — only the
                        signal-side ground (PUL-/DIR-/ENA-) ties to the
                        Teensy; that separation is the point of the
                        opto-isolated inputs.
```

**The `PUL-`/`DIR-`/`ENA-` ground return is easy to skip and the hardest
symptom to diagnose**: all three TB6600 control inputs are opto-isolated
and need a complete circuit, not just the `+` side driven — with `ENA-`
floating, many TB6600 clones default to always-enabled regardless of what
the firmware commands, which reads as "ignores Enable/Disable" rather
than "wiring problem." Get this board's own DIP switch table from its
printed datasheet/silkscreen rather than assuming a "standard" layout;
they vary (some are current-then-microstep, some the reverse).

## Known gaps / Troubleshooting

- No acceleration ramp — `backend_stepdir.cpp` steps at a constant rate
  capped by `max_pulse_rate_hz`; tune that value down before attaching a
  real motor, since an abrupt start/stop at a high rate can stall or skid
  a stepper. Ramping is unstarted follow-up work.
- `max_pulse_rate_hz` (move speed) is a boot-time-only config value pushed
  once via `CONFIGURE_CHANNEL` at `Init()`, not adjustable per-command
  through the position topic — to change it, edit the preset's
  `max_pulse_rate_hz` and restart the launcher. Find your ceiling by
  increasing gradually and watching/listening for the motor **stalling**
  (a harsh grinding/skipping sound where it stops actually turning
  despite commands still being sent) — that point depends on the motor's
  torque curve, the driver's current setting, and supply voltage, none of
  which can be predicted from the firmware side.
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
  actually wired to Teensy GND, not floating (see Wiring / Pinout above).
- If real-hardware behavior contradicts what the source clearly says
  (e.g. a guard that should prevent something is visibly not preventing
  it), force a clean rebuild (`rm -rf .pio/build .pio/libdeps && pio run
  --target upload`) before spending time debugging the "bug" in the
  source — a stale incremental PlatformIO build silently running old code
  has caused exactly this before.
- **TODO: no build/flash version identifier.** `FirmwareSpec` only carries
  `min_proto_version` (wire protocol version) — there's no way to ask a
  live board which actual build/commit is flashed on it. See
  `docs/BOARD_LAYER_RFC.md` §12 item 9 for the proposed direction
  (git-derived build id, diagnostic-only, reported over `IDENTIFY`).

## Related files

- `robot/board/teensy/teensy_board.*` — paired host-side board class; a
  thin `JoshuaWireBoard` subclass supplying only `ExpectedBoardType()`
  (`TEENSY41`) and `ExpectedWireBoardId()` (`JW1_BOARD_TEENSY41`) —
  everything else is inherited, see below
- `robot/board/joshua_wire/joshua_wire_board.*` — the shared IDENTIFY
  handshake, `CONFIGURE_CHANNEL` push, and channel dispatch every
  joshua_wire_v1 host board (Teensy today, Arduino/ESP32 later) runs
  through unchanged (docs/BOARD_LAYER_RFC.md §7.3)
- `robot/board/teensy/teensy_driver_smoke.cc` — board-level smoke test,
  bypasses ActionFactory/ROS entirely (`bazel run
  //robot/board/teensy:teensy_driver_smoke -- /dev/ttyACM0`)
- `robot/action/motors/drivers/stepper_driver.*` — paired motor driver
- `firmware/common/joshua_wire_v1.{h,c}` — the shared wire codec
- `config/config_preset/example/teensy_stepper_demo.pbtxt` — example preset
