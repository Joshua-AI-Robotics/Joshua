# Teensy 4.1 STEP/DIR Bring-up Log

For "how do I flash this," see `../README.md` — it follows the repo's
`firmware/FLASHING_TEMPLATE.md` structure and is the up-to-date how-to.
This file is the detailed bring-up record: the full wiring diagram, the
DIP switch settings that actually worked, every bug the first real
hardware pass surfaced, and open gaps — kept here rather than in the
README so the README stays a clean how-to and this stays the
historical/detailed record.

## Status

**Fully verified on real hardware, including physical motor rotation**,
through the actual production path (`launcher:joshua_main` +
`ros2 topic pub`, not just raw protocol scripts):

- [x] PlatformIO installed, firmware built and flashed, board enumerates
- [x] IDENTIFY handshake verified against host `TeensyBoard::Init()` (real
      board, real firmware — `board_id=TEENSY41`, `fw_name="teensy-stepdir"`,
      `n_channels=1`, `channel_drives[0]=STEP_DIR`)
- [x] Wire protocol and GPIO pulse generation verified independently via
      `GET_FEEDBACK` (see the raw-protocol script below) — the firmware's
      internal step counter tracks every commanded move exactly
      (`round(degrees * steps_per_degree) `steps)
- [x] **Motor physically rotates** in response to
      `ros2 topic pub /teensy_stepper_1/position std_msgs/msg/Float32
      "data: 45.0"` through the real `StepperDriver` → `TeensyBoard` →
      firmware → TB6600 → NEMA17 chain, with the launcher log showing no
      errors and the shaft visibly turning

Getting from "commands accepted, GPIO pulses confirmed via
`GET_FEEDBACK`" to "motor actually turns" took a real, multi-stage
debugging session — see the two sections below. Skip to
[Wiring](#wiring-matches-srcchannel_tablec-channel-0) for the diagram
that reflects the final, working state.

## Bugs found and fixed getting this far

Real hardware surfaced bugs a software-only pass (unit tests, golden
bytes) couldn't catch. Listed roughly in the order they were found —
each one was ruled out with a specific test before moving to the next,
which is the methodology worth reusing on the next board:

**Build/flash tooling (day 1):**
1. `platformio.ini` had `platform = teensyduino` — the correct PlatformIO
   registry name is `platform = teensy`.
2. `joshua_wire_v1.c`'s self-include was the Bazel-style
   `#include "firmware/common/joshua_wire_v1.h"`; PlatformIO's library
   builder resolves includes relative to the library's own directory, so
   it must be `#include "joshua_wire_v1.h"`.
3. `firmware/common/library.json` needed a `srcFilter` excluding
   `*_test.cc` — without it PlatformIO tries to compile the host-only
   gtest file into the firmware image.
4. `node_generator/node_generator.cc`'s `IsCppDriverAvailableForAction`
   didn't list `MOTOR_STEPPER_NEMA17`, so the launcher silently routed the
   actuator to the Python backend (which doesn't know the motor type)
   instead of the C++ path this PR built. Fixed by adding the case.
5. On Linux, flashing needs `/etc/udev/rules.d/00-teensy.rules` (from
   pjrc.com) installed, and `upload_protocol = teensy-cli` set in
   `platformio.ini` — the default `teensy-gui` tries to launch a GUI
   Teensy Loader app that isn't installed on a headless dev machine.

**"Commands succeed but the motor never moves" (day 2 — the long one):**

6. **A stale incremental PlatformIO build was running, not the current
   source.** The clearest symptom: `SetTarget` moved the firmware's
   internal `position_steps` counter even though nothing had ever called
   `Enable()` — directly contradicting `StepDirService()`'s
   `if (!channel->enabled) return;` guard. `rm -rf .pio/build
   .pio/libdeps && pio run --target upload` (a full clean rebuild) fixed
   it; the enable-gate then behaved exactly as coded. **Lesson: if
   real-hardware behavior contradicts what the source clearly says, force
   a clean rebuild before spending time debugging the "bug" in the
   source** — it may not be the code actually running.
7. **`StepperDriver::Init()` never called `Enable()`** (it mirrored
   `Sts3215Driver`'s pattern of requiring an explicit torque-enable
   command first). But the demo preset drives position over a single
   Float32 topic with no path to send a separate enable command — so
   every `SetPosition` call silently no-op'd at the firmware's
   enable-gate, forever. Fixed by auto-enabling in `Init()` instead
   (matches `TiDemoDriver`'s pattern — an open-loop stepper's `ENA` pin
   has no real safety case for withholding it the way a servo's
   torque-enable register does). See
   `robot/action/motors/drivers/stepper_driver.cc`.
8. **STEP pulse width was 2us; widened to 20us.** Teensy 4.1 drives 3.3V
   logic into opto-isolated TB6600 inputs commonly spec'd for 5V — a
   too-short HIGH time may not give the opto's LED enough time to reach
   full brightness at reduced drive current. Kept as a conservative
   improvement even though the actual root cause of "no rotation" turned
   out to be item 10 below, not pulse width.
9. **The TB6600's DIP switch layout is board-specific — don't trust a
   "standard" table.** Initially decoded the switches assuming
   `S1-S3=current, S4-S6=microstep` (common on many clones); the user's
   actual board's printed datasheet showed the opposite
   (`S1-S3=microstep, S4-S6=current`). Always get the specific board's
   own table (photograph the manual/silkscreen) rather than assuming a
   convention. The working setting for this board ended up **16
   microsteps** (`S1 S2 S3 = OFF OFF ON`, 3200 pulses/rev, matching
   `stepper_config.steps_per_degree: 8.888889` in the preset) and **2.0A
   current** (`S4 S5 S6 = ON OFF OFF`, comfortably under this TB6600's
   max, matched to the NEMA17's rated current).
10. **`ENA-` (and `PUL-`, `DIR-`) must be wired to Teensy GND — this was
    the actual root cause of "clicks but doesn't rotate," then later
    "permanently locked, ignores Enable/Disable commands."** All three
    TB6600 control inputs are opto-isolated and need a complete circuit
    (`+` driven by the Teensy pin, `-` returned to the same ground) to
    respond correctly. With `ENA-` floating, the board defaulted to
    **always enabled** regardless of what the firmware commanded — proven
    by explicitly sending `DISABLE` (confirmed via the exact same code
    path that correctly toggled the onboard LED in lockstep, see the
    diagnostic technique below) and finding the shaft stayed rigid
    anyway. Once `ENA-` (and `PUL-`/`DIR-`) were tied to the same Teensy
    GND point, the motor immediately started responding correctly to
    both `Enable`/`Disable` and `SetTarget`.
11. **Two `joshua_main`/`actuator_subscriber` process pairs were running
    simultaneously**, fighting over the same `/dev/ttyACM0`, after a
    `pkill` with a `\|`-alternation pattern silently failed to match and
    left an old instance running. This alone was enough to make a
    correctly-wired, correctly-enabled setup appear to still not respond.
    Always verify with `ps aux | grep -E "joshua_main|actuator_subscriber"
    | grep -v grep` before concluding a fix didn't work — kill any
    leftovers by exact PID, not by a `pkill` pattern you haven't verified
    actually matches.

## Diagnostic techniques that isolated the bug

Two cheap, temporary firmware modifications (both reverted once the bug
was found — see git history if you want to bring either back for a future
bring-up) proved far more useful than continuing to stare at the wire
protocol, which was already confirmed working:

- **Tie the onboard LED (pin 13, `LED_BUILTIN`) to `Enable`/`Disable`.**
  Gives a visual confirmation of comms and firmware execution completely
  independent of the TB6600 — if the LED responds correctly to explicit
  `ENABLE`/`DISABLE` commands but the motor doesn't, the problem is
  downstream of the MCU (wiring, driver, motor), not in the firmware or
  wire protocol.
- **Auto-enable and continuously step one channel from `setup()`**, no
  host commands needed at all. Lets you physically swap wiring (coil
  pairs, STEP/DIR/ENA leads) live and listen/watch for a response after
  each change, without needing to re-send a command over serial every
  time.

For a repeatable, non-interactive version of "does the board/firmware/
wiring chain work," use `teensy_driver_smoke` instead of hand-rolled
diagnostics:

```bash
bazel run //robot/board/teensy:teensy_driver_smoke -- /dev/ttyACM0
```

## Hardware

- TI/PJRC Teensy 4.1
- TB6600 stepper drive
- NEMA17 stepper motor
- Bench power supply for the TB6600 (do not power the stepper from the
  Teensy's 5V/3.3V rails) — this bring-up used 9V; current DIP set to
  2.0A. 9V is the low end of most TB6600 modules' supported 9-42V range;
  a higher-voltage supply may give more headroom for reliable current
  regulation if you see weak/stalling behavior, though it wasn't needed
  here once the wiring above was fixed.

## Wiring (matches `src/channel_table.c`, channel 0)

```
Teensy 4.1 pin 2  ──► TB6600 PUL+   (STEP)
Teensy 4.1 pin 3  ──► TB6600 DIR+   (DIR)
Teensy 4.1 pin 4  ──► TB6600 ENA+   (ENABLE)
Teensy 4.1 GND    ──► TB6600 PUL-, DIR-, ENA-  (ALL THREE — see bug #10 above;
                                                this is the connection that's
                                                easy to skip and hardest to
                                                diagnose from symptoms alone)

TB6600 A+/A-, B+/B- ──► NEMA17 coil pairs (get these from the motor's
                        datasheet or a multimeter continuity check — do not
                        guess by wire color, conventions vary by
                        manufacturer; mixing wires from two different
                        coils into one pair produces exactly the
                        "clicks/vibrates but doesn't rotate" symptom that
                        cost the most debugging time here)

TB6600 VCC/GND ──► bench PSU (sized to the motor's rated current; set the
                    TB6600's current-limit DIP switches to match BEFORE
                    powering on)
```

**Do not connect Teensy GND to the PSU's power GND/VCC** — only the
signal-side ground (`PUL-`/`DIR-`/`ENA-`) ties to the Teensy. Keeping the
power and signal grounds separate is the point of the opto-isolated
inputs.

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

## Tuning `max_pulse_rate_hz` (move speed)

There's no acceleration ramp (see Known gaps), so `max_pulse_rate_hz` in
the preset's `step_dir` config is effectively "move speed" — a fixed rate
pushed once via `CONFIGURE_CHANNEL` at `Init()`, not adjustable per-command
through the position topic. To change it, edit
`config/config_preset/example/teensy_stepper_demo.pbtxt`'s
`max_pulse_rate_hz` and restart the launcher. `4000` Hz is confirmed
working on this hardware; find your own ceiling by increasing gradually
and watching/listening for the motor **stalling** (a harsh grinding/
skipping sound where it stops actually turning despite commands still
being sent) — that point depends on your motor's torque curve, the
TB6600's current setting, and supply voltage, none of which can be
predicted from the firmware side.

## Known gaps to close before trusting this near real hardware

- No acceleration ramp — `backend_stepdir.cpp` steps at a constant rate
  capped by `max_pulse_rate_hz`; starting/stopping abruptly at a high rate
  can stall or skid a real stepper.
- `GET_FEEDBACK`'s `velocity` field is the last commanded target when in
  velocity mode, not a measurement — this firmware has no encoder.
- Serial read timeouts (`kByteTimeoutMs` in `transport_serial.cpp`, the
  `AtomicRead` timeouts in `robot/comm/serial/serial.cc`) held up under
  IDENTIFY/CONFIGURE_CHANNEL/SET_TARGET/GET_FEEDBACK round trips at
  115200 baud, including multi-second continuous moves — not stress-tested
  at higher command rates or under heavier system load.
- `max_pulse_rate_hz`/move speed is a boot-time-only config value pushed
  via `CONFIGURE_CHANNEL`; there's no live per-command speed control
  through the demo's Float32 position topic (see "Tuning
  `max_pulse_rate_hz`" above).
