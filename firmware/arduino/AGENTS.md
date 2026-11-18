# Arduino — STEP/DIR firmware (not implemented)

Follow the repository [instructions](../../AGENTS.md) and applicable ancestor
`AGENTS.md` files before changing or running this subsystem.

A future Arduino image must use the current `firmware/common/joshua_wire`
codec, endpoint and runtime. There is no previous-protocol fallback. Use
`firmware/teensy/41` as the source layout reference and determine the board's
RAM/flash and bounded GPIO support before adding a target.

## Prerequisites

TODO: select a supported MCU and matching Arduino toolchain.

## Install

TODO: add the selected PlatformIO platform.

## Build

No Arduino build target exists yet.

## Flash

No image exists. Flashing is always a deliberate hardware operation.

## Verify

TODO: add native binding/backend tests, then explicit hardware verification.

## Wiring / Pinout

TODO: document valid output pins and STEP/DIR/ENABLE wiring for the selected MCU.

## Known gaps

The host factory continues to report ARDUINO_UNO as unimplemented. No codec,
firmware image, pin assignment or protocol compatibility is inferred from that
board name.

## Firmware selection, preparation, and recovery

No supported MCU/toolchain, built artifact, upload command, or boot/recovery
procedure is established. Do not infer one from `ARDUINO_UNO` in the schema.
A concrete implementation must first fill the [required board-guide format](../AGENTS.md#required-board-guide-format)
and provide build/native evidence before any deliberate hardware bring-up.

## Evidence and limitations

The host factory reports this board as unimplemented. There is no current
firmware image to hash, flash, verify, or qualify. Existing Teensy/ESP32 results
do not establish Arduino compatibility.
