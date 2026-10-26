# Teensy 4.1 — JoshuaWire revision 4 STEP/DIR firmware

This image and the host board binding use JoshuaWire 0.2.0 / wire revision 4.
The former protocol is removed; update the host and firmware from the same
checkout. Previously flashed firmware is incompatible with this host.

## Status

The revision-4 image is cross-built with PlatformIO. Native integration tests
exercise the actual firmware endpoint and STEP/DIR backend through the production
host board bindings and serial factory over PTYs, with GPIO/clock simulated.
An authorized bench run of the TX-fault fix flashed this revision and verified
USB protocol/backpressure behavior with motor power disconnected (record below).
Physical GPIO levels, pulse timing and motor stop time remain unverified.
Historical hardware bring-up of the previous protocol does not verify this image.

## Layout

- `src/main.cpp`: platform boot identity and pin validation, then the shared app.
- `src/channel_table.c`: per-image function/channel count, with no default pins.
- `src/transport_serial.cpp`: incremental bounded serial input.
- `firmware/common/joshua_stepdir_device.cpp`: discovery, profile callbacks,
  cached feedback, resource ownership, and safety using the shared endpoint.
- `firmware/common/backend_stepdir.cpp`: nonblocking STEP edges and GPIO enable.

## Prerequisites

Use the Joshua Docker development environment, PlatformIO, and the `teensy41-serial`
Arduino framework/toolchain. Hardware is needed only for deliberate flashing
and physical verification. See the wiring below before connecting a motor.

## Install

Install PlatformIO in the development container if it is not already available:

```bash
python3 -m pip install platformio
```

## Build

Inside the container:

```bash
pio run -d firmware/teensy/41
```

The shared `joshua_wire` PlatformIO library compiles the same revision-4 C
codecs, runtime and endpoint that Bazel uses on the host. No old codec is linked.

## Flash

Flashing is a deliberate hardware operation. With the intended board connected
and reviewed, upload the image explicitly:

```bash
pio run -d firmware/teensy/41 --target upload
```

The artifact is `.pio/build/teensy41-serial/firmware.hex`.

## Verify

After a human-initiated flash, review the device and wiring in
`config/config_preset/example/teensy_stepper_demo.pbtxt`, then run that preset in the
matching Docker launcher service. It selects `message_adapter: JOSHUA_WIRE` and
`min_proto_version: 4`; initialization resets the session, discovers functions,
checks profile/safety metadata and configures pins while outputs stay disabled.
A physical motion test must separately confirm directions, enable polarity,
pulse timing and stop behavior. The expected serial path is `/dev/ttyACM0`.

## Wiring / Pinout

**Pin numbers are host-configured** (`StepDirConfig.step_pin`/`dir_pin`/
`enable_pin` in the pbtxt, pushed to firmware via `actuator PROFILE/CONFIGURE` at
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

- Feedback is an open-loop pulse count and commanded rate, not encoder data.
- Position moves use the configured maximum pulse rate; no acceleration ramp.
- Configuration is rejected while enabled, for unavailable/reserved GPIOs, or
  when another function already owns a requested pin. Session reset and ESTOP
  disable GPIOs and invalidate configuration; reset preserves an ESTOP latch.
- Partial serial frames never block pulse servicing. STEP-high timing uses
  nonblocking edge servicing so dispatch and stop can run between edges.
- Physical stop verification is not supported or advertised.

## Bounded serial I/O and native tests

The serial loop uses a retained transmit slot and keeps servicing STEP/DIR while
UART output is blocked. Incoming frames use the shared revision-4 decoder; CRC,
length and version failures never dispatch commands. Native `serial_test` builds
the real setup/loop and transport with test-only GPIO, serial and entropy APIs.
It does not qualify physical pulse timing or hardware stop behavior.

A response retained in the adapter for 20 ms triggers a latched local fault,
disabling and unconfiguring all channels even if a queued ESTOP cannot be read.
See the shared [serial fault budget](../../common/README.md#serial-transmit-fault-budget)
for service timing, status, reply retention and explicit recovery requirements.

The manual `//robot/board/joshua_wire:joshua_wire_smoke` CLI supports handshake,
disabled configuration and opt-in exercise modes. Review `--help` and use
`--dry_run` first; hardware execution requires explicit confirmation. Recovery
of a latched ESTOP also requires `--allow_recover`.

## TX-fault bench record — 2026-10-25

- Board: Teensy USB serial `15104350`, `/dev/ttyACM0`. Motor power was confirmed
  disconnected before the authorized flash and stationary output-enable tests.
- Image: `teensy41-serial/firmware.hex`, SHA-256
  `19734e2e0dcd42ab50aa6a61f087b24ac7ad6f62b6d95b0371499304c1f96159`.
- Docker Ubuntu 24 built/flashed the image. Ubuntu 22's production
  `joshua_wire_smoke` passed reset, identity, disabled configuration, feedback
  and ESTOP cleanup. The bench config used the example preset's GPIO 2/3/4,
  active-low enable, 4000 Hz maximum rate and 20 us pulse width, with explicit
  `protocol: JOSHUA_WIRE` added to a temporary copy for the diagnostic preflight.
- A raw USB probe used the checkout's C frame codec, enabled channel 0 without
  a motion target, queued 4096 exact IDENTIFY requests and withheld reads for
  150 ms. After draining, board and function `GET_STATUS` reported
  `UNAVAILABLE / DEVICE_FAULT`, `ACTION_COMPLETE`, recovery mask 7, no host
  action token and unsupported physical verification. All 4096 correlated
  IDENTIFY replies were intact and byte-identical.
- A second burst appended ESTOP behind the same 4096 retries. The function
  epoch advanced twice: local fault, then the correlated queued ESTOP. All
  IDENTIFY replies and the ESTOP response arrived intact. Fresh ENABLE was
  rejected after fault and after session reset; RECOVER alone did not enable
  outputs or permit ENABLE without reconfiguration.
- Final ESTOP cleanup succeeded; the board was left unavailable and latched,
  requiring recovery, configuration and explicit enable before reuse.

This establishes real USB backpressure, reply retention and reported fault/stop
transitions. Without GPIO instrumentation it does not measure the 20 ms output
disable deadline or establish a physical motor stop guarantee.

## Partial-write fix bench record — 2026-10-25

The same authorized stationary bench checks were repeated after flashing the
partial-write fix to Teensy `15104350`, with motor power disconnected. Image
SHA-256:
`181565393a599ff3438db8e05f525ab6182650de5f83d70f6f16f58ce687e39e`.

The production configure/feedback diagnostic passed. Both 4096-retry bursts
drained intact with byte-identical IDENTIFY replies. With host reads withheld,
the runtime reported the local fault; the burst with a queued ESTOP showed the
local-fault and subsequent ESTOP transitions. Reset/recovery interlocks passed,
and final ESTOP cleanup left the board latched and unavailable.

This is a hardware regression check for the new image. The specific 16-byte TX
capacity case is covered by native tests of the actual Teensy and ESP32 bindings;
this bench run does not establish that the physical Teensy's writable capacity
was 16 bytes. GPIO stop timing and physical motor stop remain unmeasured.
