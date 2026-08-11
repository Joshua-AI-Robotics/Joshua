# Joshua Firmware

This directory tracks firmware that Joshua expects on external boards.

Joshua can own two different kinds of firmware records:

- Joshua-owned firmware source and shared codecs that can be built from this
  repository.
- Metadata for vendor or bring-up firmware that Joshua can validate against,
  but should not vendor until redistribution rights are clear.

Runtime code must not flash boards automatically. Flashing is a board-management
operation that will eventually live behind `tools/flash/` and a firmware
manifest. Runtime board initialization should detect a mismatch and report the
firmware/artifact needed to fix it.

## How to flash a board

Every board's README under `firmware/<board>/` (or `firmware/<board>/<version>/`
for versioned boards) follows the same structure — Prerequisites, Install,
Build, Flash, Verify, Wiring/Pinout, Known gaps — defined in
[`FLASHING_TEMPLATE.md`](FLASHING_TEMPLATE.md). Start there for "how do I
flash board X"; where a board hasn't been brought up yet, its README says
so explicitly with `TODO` placeholders rather than staying silent.

## Current Firmware Records

| Board | Status | README |
| --- | --- | --- |
| AM243 (LP-AM243, TI EtherCAT demo) | Hello World + EtherCAT slave demo built, flashed, verified on real hardware. Vendor firmware — metadata only, stays as-is. | [`am243/ti_ethercat_simple_demo_v1/README.md`](am243/ti_ethercat_simple_demo_v1/README.md) |
| Teensy 4.1 (STEP/DIR over `joshua_wire_v1`) | Built, flashed, verified end to end on real hardware, including physical motor rotation through the real production path. | [`teensy/41/README.md`](teensy/41/README.md) |
| Arduino (STEP/DIR over `joshua_wire_v1`) | Not started — real future board (`docs/BOARD_LAYER_RFC.md` §10 Phase 5), not retired by Teensy being first. | [`arduino/README.md`](arduino/README.md) |
| ESP32 (STEP/DIR over `joshua_wire_v1`) | Built, joins the same joshua_wire_v1 family as Teensy (plain UART/USB-serial, not the Wi-Fi/UDP transport variant once speculated) — not yet flashed or run against real hardware. | [`esp32/README.md`](esp32/README.md) |

- `common/joshua_wire_v1.{h,c}`: the shared frame codec between Joshua host
  boards and Joshua-authored MCU firmware (docs/BOARD_LAYER_RFC.md §7.2/§7.3).
  Built as a Bazel `cc_library` for the host and as a PlatformIO library
  (`library.json`) for every firmware target — same two files, two
  toolchains, one repo commit.

## Layout

```text
firmware/
  FLASHING_TEMPLATE.md   the section structure every board README follows
  common/       # shared host/firmware wire codec (joshua_wire_v1)
  am243/        # vendor TI demo firmware; metadata only, stays as-is
  teensy/41/    # Joshua-owned firmware for the Teensy 4.1
  arduino/      # not started — placeholder README only
  esp32/        # Joshua-owned firmware for ESP32, built, not yet flashed
```

Firmware variants should be explicit build artifacts, not runtime-selected
bundles. For example, an Ethernet variant and a serial variant should build as
separate artifacts with names that identify the board, transport, and protocol
version.
