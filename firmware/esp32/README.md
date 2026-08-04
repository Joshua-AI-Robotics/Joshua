# ESP32 firmware (not started)

**Placeholder.** Not started, not even scoped in detail yet — tracked in
`docs/BOARD_LAYER_RFC.md` §10 Phase 5 as a candidate third matrix member.
Most likely lands as a Wi-Fi/UDP `FrameTransport` variant (proving the
transport-swap seam already built for Teensy/Arduino) rather than a new
drive backend — i.e. probably reuses `StepperDriver` and a STEP_DIR
channel table, with a new `transport_udp.*` and a new host
`UdpFrameTransport`, not a new motor driver. Confirm that framing before
starting real work here.

## Layout

TODO

## Status

- ⬜ Board type / use case decided (see note above — likely a transport
  proof, not a new drive backend)
- ⬜ Toolchain installed
- ⬜ Firmware built
- ⬜ Firmware flashed
- ⬜ Protocol/handshake verified against the host
- ⬜ Full command path verified end to end

## Prerequisites

TODO

## Install

TODO

## Build

TODO

## Flash

TODO

## Verify

TODO

## Wiring / Pinout

TODO

## Known gaps / Troubleshooting

- Not started at all — this file exists so the RFC's mention of ESP32 as
  a candidate matrix member has a landing spot, not because any design
  work has happened yet.

## Related files

- `docs/BOARD_LAYER_RFC.md` §10 Phase 5 — where this is tracked
- `firmware/teensy/41/` — the worked example for the STEP_DIR/serial side
  of this board, if ESP32 does end up reusing that channel shape
