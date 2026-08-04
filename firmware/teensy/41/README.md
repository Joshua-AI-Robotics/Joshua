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
    channel_table.c       THE pinout contract (docs/BOARD_LAYER_RFC.md §7.5)
    channel_table.h
    backend_stepdir.{h,cpp}   STEP/DIR/ENA pulse generation
    transport_serial.{h,cpp} joshua_wire_v1 framing over Serial
  docs/bringup.md        build, flash, wire, verify
```

`joshua_wire_v1.{h,c}` itself is not copied here — `platformio.ini` pulls it
in directly from `firmware/common/` via `lib_deps = symlink://../../common`,
so this firmware and the host (`//firmware/common:joshua_wire_v1` in Bazel)
always build from the exact same two files.

## Build & flash

```bash
cd firmware/teensy/41
pio run                              # build
pio run --target upload              # flash over USB
pio device monitor -b 115200         # optional: watch for crashes/prints
```

See `docs/bringup.md` for wiring, PlatformIO install, and end-to-end
verification against the host `am243_config_smoke`-style flow.

## Status

Implemented, unit-tested on the host side (`teensy_board_test.cc`,
`joshua_wire_v1_test.cc` golden-bytes tests), and **verified against real
Teensy 4.1 hardware**: built, flashed, IDENTIFY handshake round-tripped
correctly, and a real `ros2 topic pub` position command produced real STEP
pulses on the configured pins, confirmed via an independent `GET_FEEDBACK`
query. See `docs/bringup.md`'s status checklist for exactly what's
verified vs. still open (notably: no TB6600 was wired for this pass, so
GPIO pulses are confirmed but actual motor rotation is not yet observed).
