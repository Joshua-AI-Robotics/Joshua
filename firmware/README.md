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

## Future Layout

The board-layer RFC proposes this direction:

```text
firmware/
  common/          # shared host/firmware wire and PDO codecs
  am243/           # AM243 firmware source or metadata
  arduino_tb6600/  # Joshua-owned small-MCU firmware variants
```

Firmware variants should be explicit build artifacts, not runtime-selected
bundles. For example, an Ethernet variant and a serial variant should build as
separate artifacts with names that identify the board, transport, and protocol
version.
