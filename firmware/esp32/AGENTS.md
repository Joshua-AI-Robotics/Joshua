# ESP32 — JoshuaWire revision 4 STEP/DIR firmware

Follow the repository [instructions](../../AGENTS.md) and applicable ancestor
`AGENTS.md` files before changing or running this subsystem.

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

## Firmware selection

| Environment | Artifact | Role |
| --- | --- | --- |
| `esp32-serial` | `.pio/build/esp32-serial/firmware.bin` | JoshuaWire UART, STEP/DIR |

Artifact paths are relative to `firmware/esp32`. Use host and firmware from the
same checkout; wire revision 4 and codec 0.2.0 are required. Images have different
capabilities; changing the environment is an explicit setup change.

## Prerequisites and install

Follow the [shared toolchain environment](../AGENTS.md#toolchain-environment).
Use a data-capable USB cable and the exact ESP32 board named above. Record
resolved packages from [platformio.ini](platformio.ini); building needs no board.

## Build

From the repository root inside the container, select one environment:

```bash
JOSHUA_ENV=esp32-serial
pio run -d firmware/esp32 -e "$JOSHUA_ENV"
pio pkg list -d firmware/esp32 -e "$JOSHUA_ENV"
sha256sum firmware/esp32/.pio/build/"$JOSHUA_ENV"/firmware.bin
```

## Preparation

Identify the USB-UART adapter by its serial identity and board labeling inside
the authorized container. `pio device list --json-output` lists serial metadata;
it does not prove board wiring. Set the actual stable path, verify its target,
and close other owners. The placeholder below must be replaced:

```bash
JOSHUA_PORT=/dev/serial/by-id/REPLACE_WITH_VERIFIED_ESP32
readlink -f "$JOSHUA_PORT"
```

Use the wiring/power requirements below and the shared authorization rules.
DevKitC automatic DTR/RTS reset normally selects download mode. If unavailable,
follow [Espressif boot-mode selection](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html):
hold BOOT/GPIO0 low during an EN/reset, then release it for normal boot.

## Flash

Only for the currently authorized, identified board, in the same shell:

```bash
pio run -d firmware/esp32 -e "$JOSHUA_ENV" --target upload --upload-port "$JOSHUA_PORT"
```

Require exit success and retain the esptool write/verification log. PlatformIO
uploads the environment's complete image set; do not write `firmware.bin` to a
guessed raw offset. See the [upload-port option](https://docs.platformio.org/en/latest/core/userguide/cmd_run.html#cmdoption-pio-run-upload-port).

## Return to normal boot

Release BOOT and reset/power-cycle as needed. Re-identify the USB-UART path;
normal flash boot must replace download mode. Do not start motor commands merely
because the upload succeeded.

## Verify

First confirm enumeration and the selected artifact/host revision. Use the
[serial preflight and handshake](../../docs/JOSHUA_WIRE_VALIDATION.md#build-and-preflight-no-hardware-access):
validate the reviewed protobuf board config with `--dry_run`, then perform an
explicitly authorized handshake with `--confirm_hardware`. The default handshake
resets/identifies/resets without configuring or enabling channels.

Configuration can change GPIO even with motor power disconnected. Follow the
validation guide for any further configuration or powered motion. A successful
handshake does not establish wiring, direction, pulse timing, or physical stopping.

## Recovery and evidence

Check cable/data access, container permissions, exclusive loader access, selected
environment, and board identity before retrying. Recover the boot mode using the
board procedure above; rebuild/reselect an image instead of guessing compatibility.
Record the commit, artifact SHA256, tool/package versions, board identity, upload
command/result, boot result, handshake result, power state, and unresolved gaps.
Keep the historical qualification records below distinct from this new run.

## Wiring / Pinout

**Pin numbers are host-configured** (`StepDirConfig.step_pin`/`dir_pin`/
`enable_pin` in the pbtxt, pushed to firmware via `actuator PROFILE/CONFIGURE` at
`Init()`), not hardcoded in `channel_table.c`
(`robot/board/AGENTS.md`). `channel_table.c` only declares
how many channel *slots* this firmware image has — still compile-time,
since that's reported by `IDENTIFY`.

`config/config_preset/example/esp32_stepper_demo.pbtxt` uses GPIO 25
(STEP), 26 (DIR), 27 (ENABLE) — ordinary general-purpose output pins on a
standard ESP32 DevKitC-style board: not strapping pins (0, 2, 5, 12, 15),
not input-only (34–39), not the default UART0/flash pins. A reasonable
starting choice, not a requirement — rewire and edit the pbtxt freely.

Reference wiring against a TB6600 (same shape as
`firmware/teensy/41/AGENTS.md`'s, different pin numbers):

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
(`firmware/teensy/41/AGENTS.md`'s Wiring / Pinout section) almost
certainly apply here too, since they're facts about the TB6600, not the
MCU — but treat that as a prediction until it's actually been run.

## Known gaps / Troubleshooting

- Feedback is an open-loop pulse count and commanded rate, not encoder data.
- Position moves use the configured maximum pulse rate; no acceleration ramp.
- Configuration is rejected while enabled, for unavailable/reserved GPIOs, or
  when another function already owns a requested pin. Session reset and ESTOP
  disable GPIOs and invalidate configuration; reset preserves an ESTOP latch.
- Partial serial frames never block pulse servicing. STEP-high timing uses
  nonblocking edge servicing so dispatch and stop can run between edges.
- Physical stop verification is not supported or advertised.
- USB-UART bridges may reset the MCU on open. The preset declares
  `post_open_settle_ms: 2000`, applied once by the shared serial mechanism.
- UART0 pins 1/3, flash pins 6–11 and input-only GPIOs cannot be actuator outputs.

## Bounded serial I/O and native tests

The serial loop uses a retained transmit slot and keeps servicing STEP/DIR while
UART output is blocked. Incoming frames use the shared revision-4 decoder; CRC,
length and version failures never dispatch commands. Native `serial_test` builds
the real setup/loop and transport with test-only GPIO, serial and entropy APIs.
It does not qualify physical pulse timing or hardware stop behavior.

A response retained in the adapter for 20 ms triggers a latched local fault,
disabling and unconfiguring all channels even if a queued ESTOP cannot be read.
See the shared [serial fault budget](../common/AGENTS.md#serial-transmit-fault-budget)
for service timing, status, reply retention and explicit recovery requirements.

The manual `//robot/board/joshua_wire:joshua_wire_smoke` CLI supports handshake,
disabled configuration and opt-in exercise modes. Review `--help` and use
`--dry_run` first; hardware execution requires explicit confirmation. Recovery
of a latched ESTOP also requires `--allow_recover`.

Actuator payload decoding, supported-mode checks, admission guards, replies and
outcome reporting use the portable C `joshua_actuator_handler`. Device callbacks
retain pin validation, resource ownership, target limits, cached acquisition and
hardware actions. Teensy/ESP32 register these callbacks in `joshua_stepdir_device`;
AM243 uses `am243_software_backend` and has no physical STEP/DIR output.
