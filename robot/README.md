# Robot

Hardware-facing code: everything between a `.pbtxt` config and a physical
motor, sensor, or bus. ROS 2 nodes live in [ros2/](../ros2/README.md) and
consume these interfaces; nothing here depends on ROS 2 message types.

> This is the code that moves physical motors and opens real buses. Before
> running anything from here, see the hardware-safety section of
> [AGENTS.md](../AGENTS.md).

## Layers

```
action/       what moves      motor semantics — degrees, limits, calibration
board/        what runs the   control-loop owner: bring-up, codec, channel mux
              loop
comm/         how bytes move  serial, EtherCAT — transport only
perception/   what senses     cameras, encoders, lidar
```

`board/` is **mid-migration — read this before touching the actuator path.**
Each layer talks to the one below through an interface, never a concrete type:
a motor driver holds a `BoardChannel`, not a `Serial`, so a motor, a controller
board, and a transport can be chosen independently in config rather than in
code.

The actuator path is there: every `MotorType` `ActionFactory` supports resolves
`board_name` → `BoardFactory` → `OpenChannel` → driver, over the shared lookup
in `board/factory/board_resolver.h`.

Perception resolves the same way (Phase 6), and a sensor reaches hardware over
one of exactly two legs — which one is a property of the device, not a
preference:

- **Board leg.** The device multiplexes several channels over one link (a
  Feetech servo bus, an MCU, an EtherCAT slave), so it needs channel
  addressing and bus arbitration: it *is* a board. The sensor names one and
  opens a channel on it, so a sensor and an actuator on one bus share a board
  instance and its bus mutex.
- **Device leg.** The device multiplexes nothing (a camera, a scanning lidar).
  There is no channel to address and no bus to share, so it owns its own
  handle — a `robot::comm::StreamTransport` for anything that streams bytes,
  which keeps the comm axis a config choice.

Sensors are named for what they measure, never for how they are wired, which
is why one `JointPositionSensor` serves a Feetech register read, a quadrature
counter and a PDO slot. Check
[docs/BOARD_LAYER_RFC.md](../docs/BOARD_LAYER_RFC.md) §10 for which phase has
landed before assuming either way.

**There is no Python in this directory, and none should be added.** The Python
robot layer (factories, interfaces, mock drivers) was deleted in RFC §10
Phase 9, and the Pybricks bench driver moved to
[tools/pybricks/](../tools/README.md) as off-runtime-path tooling.
Hardware-facing ROS 2 nodes are C++ only, and `node_generator` no longer
selects between backends.

## Responsibilities

- `action/` — motor drivers (`motors/drivers/`), the actuator interfaces, and
  `factory/`, which resolves a config actuator to a driver.
- `board/` — `interfaces/` (`BoardChannel`, `BoardInterface`), and `factory/`
  with its per-board instance cache, the shared channel resolver, and the
  motor/drive and sensor/signal compatibility tables,
  `proto/`, and `mock/`. `mock/` is C++ test infrastructure: it lets the
  factory and board tests exercise real drivers with no hardware attached.
- `comm/` — `interfaces/` (`StreamTransport`), `serial/` and `ethercat/`
  transports, plus `factory/`. Transports move bytes and know nothing about
  motors or sensors.
- `perception/` — sensor drivers behind the single `PerceptionInterface`
  seam: `sensors/` for board-attached sensors (`JointPositionSensor` over a
  `BoardChannel`, owning no port), `camera/` and `lidar/` for single-stream
  devices, and `factory/`, which picks the leg and resolves the sensor.

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
