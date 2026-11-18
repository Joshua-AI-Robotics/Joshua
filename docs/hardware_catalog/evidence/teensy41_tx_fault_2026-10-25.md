## TX-fault bench record — 2026-10-25

- Board: Teensy USB serial `15104350`, `/dev/ttyACM0`. Motor power was confirmed
  disconnected before the authorized flash and stationary output-enable tests.
- Image: `teensy41-serial/firmware.hex`, SHA-256
  `19734e2e0dcd42ab50aa6a61f087b24ac7ad6f62b6d95b0371499304c1f96159`.
- Docker Ubuntu 24 built/flashed the image. Ubuntu 22's production
  `joshua_wire_smoke` passed reset, identity, disabled configuration, feedback
  and ESTOP cleanup. The bench config used the example preset's GPIO 2/3/4,
  active-low enable, 4000 Hz maximum rate and 20 us pulse width, with explicit
  `protocol: JOSHUA_WIRE` added to a temporary copy for the diagnostic preflight.
- A raw USB probe used the checkout's C frame codec, enabled channel 0 without
  a motion target, queued 4096 exact IDENTIFY requests and withheld reads for
  150 ms. After draining, board and function `GET_STATUS` reported
  `UNAVAILABLE / DEVICE_FAULT`, `ACTION_COMPLETE`, recovery mask 7, no host
  action token and unsupported physical verification. All 4096 correlated
  IDENTIFY replies were intact and byte-identical.
- A second burst appended ESTOP behind the same 4096 retries. The function
  epoch advanced twice: local fault, then the correlated queued ESTOP. All
  IDENTIFY replies and the ESTOP response arrived intact. Fresh ENABLE was
  rejected after fault and after session reset; RECOVER alone did not enable
  outputs or permit ENABLE without reconfiguration.
- Final ESTOP cleanup succeeded; the board was left unavailable and latched,
  requiring recovery, configuration and explicit enable before reuse.

This establishes real USB backpressure, reply retention and reported fault/stop
transitions. Without GPIO instrumentation it does not measure the 20 ms output
disable deadline or establish a physical motor stop guarantee.
