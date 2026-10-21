# Teensy 4.1 — JoshuaWire revision 4 STEP/DIR firmware

This image and the host board binding use JoshuaWire 0.2.0 / wire revision 4.
The former protocol is removed; update the host and firmware from the same
checkout. Previously flashed firmware is incompatible with this host.

## Status

The revision-4 image is cross-built with PlatformIO. Native integration tests
exercise the actual firmware endpoint and STEP/DIR backend through the production
host board bindings and serial factory over PTYs, with GPIO/clock simulated.
This revision has not been flashed or motion-verified by that test workflow.
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
