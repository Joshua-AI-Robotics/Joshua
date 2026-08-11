# Arduino — joshua_wire_v1 STEP/DIR firmware (not started)

**Placeholder.** Not built yet — tracked in `docs/BOARD_LAYER_RFC.md` §10
Phase 5 as a real future board, not retired by Teensy 4.1 being the first
one built. Same architecture as `firmware/teensy/41/`: same
`joshua_wire_v1` codec, same `StepperDriver`, same `FrameTransport` seam.
The host board class is now genuinely small: `TeensyBoard`'s IDENTIFY
handshake, `CONFIGURE_CHANNEL` push, and channel dispatch were extracted
into `JoshuaWireBoard` (`robot/board/joshua_wire/`, docs/BOARD_LAYER_RFC.md
§7.3) specifically so this board wouldn't need to reimplement them —
`ArduinoBoard` (`robot/board/arduino/`, not yet created) only needs to
subclass `JoshuaWireBoard` and supply two methods:
`ExpectedBoardType() → BoardType::ARDUINO_UNO` and
`ExpectedWireBoardId() → JW1_BOARD_ARDUINO_UNO` (already reserved in
`joshua_wire_v1.h`). See `robot/board/teensy/teensy_board.h/.cc` for
exactly how short that subclass is in practice. The real new work here is
the firmware image, not the host side. Follow `firmware/teensy/41/` as the
worked example when starting this.

## Layout

TODO — expected to mirror `firmware/teensy/41/`:

```text
firmware/arduino/
  platformio.ini
  src/
    main.cpp
    channel_table.c        channel *count* only — pins are host-configured,
                           not here (docs/BOARD_LAYER_RFC.md §7.5, revised;
                           see firmware/teensy/41/ for the pattern to copy)
    channel_table.h
    backend_stepdir.{h,cpp}
    transport_serial.{h,cpp}
  docs/bringup.md
```

## Status

- ⬜ Toolchain installed
- ⬜ Firmware built
- ⬜ Firmware flashed
- ⬜ Board enumerates
- ⬜ Protocol/handshake verified against the host
- ⬜ Full command path verified end to end

## Prerequisites

- Hardware: TODO (which Arduino board specifically — the RFC's directory
  layout target names `ARDUINO_UNO` as the `BoardType`, so likely an Uno
  or Uno-compatible board; confirm before starting)
- OS packages: TODO
- Toolchain: PlatformIO (same as Teensy — see `firmware/teensy/41/README.md`
  for the install steps, which should transfer directly)
- Accounts: TODO

## Install

TODO

## Build

TODO

## Flash

TODO — likely a standard AVR bootloader flash over serial (`avrdude`, via
PlatformIO), not the Teensy-specific HalfKay/`teensy_loader_cli` flow.

## Verify

TODO — same pattern as `firmware/teensy/41/README.md`'s Verify section
should apply: enumeration, then IDENTIFY via `ArduinoBoard::Init()` (not
yet written — but inherited unchanged from `JoshuaWireBoard::Init()`, so
"not yet written" really just means "not yet subclassed"), then a real
command round-trip.

## Wiring / Pinout

TODO — will be host-configured via `StepDirConfig.step_pin`/`dir_pin`/
`enable_pin` in the pbtxt (same as Teensy), not hardcoded in
`channel_table.c`, per `docs/BOARD_LAYER_RFC.md` §7.5 (revised).

## Known gaps / Troubleshooting

- Not started. `robot/board/arduino/ArduinoBoard` does not exist yet
  either — see `docs/BOARD_LAYER_RFC.md` §10 Phase 5. When it's created,
  it should be a thin `JoshuaWireBoard` subclass (see the note at the top
  of this file), not a fresh `BoardInterface` implementation.

## Related files

- `firmware/teensy/41/` — the worked example this board should mirror
- `firmware/common/joshua_wire_v1.{h,c}` — the shared wire codec (reused
  as-is, no changes needed)
- `robot/board/joshua_wire/joshua_wire_board.*` — the shared host-side
  IDENTIFY/CONFIGURE_CHANNEL/channel-dispatch orchestration `ArduinoBoard`
  should subclass (reused as-is, no changes needed)
- `robot/action/motors/drivers/stepper_driver.*` — the motor driver
  (reused as-is, no changes needed)
- `robot/board/arduino/arduino_board.*` — TODO, not yet created; should be
  ~2 methods (see `robot/board/teensy/teensy_board.h/.cc` for the pattern)
