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

## Current Firmware Records

- `am243/ti_ethercat_simple_demo_v1.md`: firmware record for the TI EtherCAT
  simple demo used to validate the LP-AM243 EtherCAT smoke path.
- `am243/ti_ethercat_simple_demo_v1/`: checked-in wrapper scripts, setup
  templates, and bring-up notes for reproducing that board image.
- `common/joshua_wire_v1.{h,c}`: the shared frame codec between Joshua host
  boards and Joshua-authored MCU firmware (docs/BOARD_LAYER_RFC.md §7.2/§7.3).
  Built as a Bazel `cc_library` for the host and as a PlatformIO library
  (`library.json`) for every firmware target — same two files, two
  toolchains, one repo commit.
- `teensy/41/`: Joshua-owned firmware for a Teensy 4.1 driving STEP/DIR
  channels (TB6600 or equivalent) over `joshua_wire_v1` on native USB
  serial (docs/BOARD_LAYER_RFC.md §10 Phase 5). Paired host class:
  `robot/board/teensy/teensy_board.*`.

## Layout

```text
firmware/
  common/       # shared host/firmware wire codec (joshua_wire_v1)
  am243/        # vendor TI demo firmware; metadata only, stays as-is
  teensy/41/    # Joshua-owned firmware for the Teensy 4.1
```

A future Arduino variant (`firmware/arduino/`, per docs/BOARD_LAYER_RFC.md
§5.2/§10 Phase 5) and a future ESP32 variant would land the same way: their
own `firmware/<board>/` directory, their own host `robot/board/<board>/`
class, both built against the same `joshua_wire_v1` codec.

Firmware variants should be explicit build artifacts, not runtime-selected
bundles. For example, an Ethernet variant and a serial variant should build as
separate artifacts with names that identify the board, transport, and protocol
version.
