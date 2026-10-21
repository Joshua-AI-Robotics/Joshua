# Arduino — STEP/DIR firmware (not implemented)

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
