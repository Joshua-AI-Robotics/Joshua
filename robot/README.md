# Robot

Hardware-facing code: everything between a `.pbtxt` config and a physical
motor, sensor, or bus. ROS 2 nodes live in [ros2/](../ros2/README.md) and
consume these interfaces; nothing here depends on ROS 2 message types.

## Layers

```
action/       what moves      motor semantics — degrees, limits, calibration
board/        what runs the   control-loop owner: bring-up, codec, channel mux
              loop
comm/         how bytes move  serial, EtherCAT — transport only
perception/   what senses     cameras, encoders, lidar
```

`board/` is **mid-migration — read this before touching the actuator path.**
The intended end state is that each layer talks to the one below through an
interface, never a concrete type: a motor driver holds a `BoardChannel`, not a
`Serial`, so a motor, a controller board, and a transport can be chosen
independently in config rather than in code.

That is not yet true. What has landed is the `board/` skeleton —
`BoardChannel`, `BoardInterface`, the factory with its instance cache and
motor/channel validation, and a mock. Its only consumers today are its own
tests. The live actuator path still predates it: `Sts3215Driver` takes a
`Serial`, `Am243EthercatDriver` takes an `EthercatTransport`, and
`ActionFactory` builds those transports through `CommFactory` directly.

So when you read a motor driver, expect the old wiring. When you write one,
check [docs/BOARD_LAYER_RFC.md](../docs/BOARD_LAYER_RFC.md) §10 for which phase
has landed.

## Responsibilities

- `action/` — motor drivers (`motors/drivers/`), the actuator interfaces, and
  `factory/`, which resolves a config actuator to a driver.
- `board/` — `interfaces/` (`BoardChannel`, `BoardInterface`), `factory/` with
  its per-board instance cache and motor/channel compatibility validation,
  `proto/`, and `mock/` for tests. Not yet on the runtime path (see above).
- `comm/` — `serial/` and `ethercat/` transports plus `factory/`. Transports
  move bytes and know nothing about motors.
- `perception/` — camera, encoder, and lidar drivers behind
  `perception/interfaces/`.

## Non-Goals

- ROS 2 node lifecycle, topics, or message types — see [ros2/](../ros2/README.md).
- Robot-specific values. Joint names, limits, ports, and calibration come from
  the protobuf config ([config/](../config/README.md)), never from constants
  in a driver.
- Firmware. Board firmware and flashing live in
  [firmware/](../firmware/README.md); runtime code never flashes a board.

## Before you change this

The board layer is a staged migration — read
[docs/BOARD_LAYER_RFC.md](../docs/BOARD_LAYER_RFC.md) before adding a motor
type, a board, or a transport, and check which phase has landed. Adding one of
those should mean **one new file in one layer**, not a new enum value threaded
through several. EtherCAT specifics are in
[comm/ethercat/README.md](comm/ethercat/README.md).
