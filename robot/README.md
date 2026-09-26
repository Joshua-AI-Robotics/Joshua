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
`board_name` → `BoardFactory` → `OpenChannel` → driver. Position sensors use the same board/channel resolver and shared board cache.
Cameras use OpenCV directly; LDS01 lidar acquires a byte stream from CommFactory.
All perceptions select a concrete driver through `SinglePerception.sensor_config`;
the legacy camera/encoder/lidar config wrappers have been removed.

**There is no Python in this directory, and none should be added.** The Python
robot layer (factories, interfaces, mock drivers) was deleted in RFC §10
Phase 9, and the Pybricks bench driver moved to
[tools/pybricks/](../tools/README.md) as off-runtime-path tooling.
Hardware-facing ROS 2 nodes are C++ only, and `node_generator` no longer
selects between backends.

## Responsibilities

- `action/` — motor drivers (`motors/drivers/`), the actuator interfaces, and
  `factory/`, which resolves a config actuator to a driver.
- `board/` — `interfaces/` (`BoardChannel`, `BoardInterface`), `factory/` with
  its per-board instance cache and motor/channel compatibility validation,
  `proto/`, and `mock/`. `mock/` is C++ test infrastructure: it lets the
  factory and board tests exercise real drivers with no hardware attached.
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

Adding a motor type, board, or transport should mean **one new file in one
layer**, not a new enum value threaded through several. Communication mechanism
and capability boundaries are described in [comm/README.md](comm/README.md).
EtherCAT specifics are in [comm/ethercat/README.md](comm/ethercat/README.md).

## Joint commands

`ActionPacket.joint` is the motion payload for every actuator. Optional position,
velocity, and effort distinguish omission from zero. `units: NATIVE` is the
default: position uses existing driver units and velocity retains the driver's
nonnegative move-speed setting. `units: SI` represents physical position,
velocity, and effort. ROS JointState decoding sets SI explicitly and preserves
joint name, frame, timestamp, and every supplied numeric field.

STS3215 and stepper support native position/velocity combinations and SI
position-only commands (radians converted to ticks/degrees). They reject effort;
use presets to enable/disable torque. TI demo supports native position/velocity/
effort using its existing firmware scaling, but rejects SI commands. Drivers
validate the entire payload before writes. Channel failures are returned; a
multi-field command is not a transactional hardware operation.

Scalar topics retain native values: `/position` maps to position, `/speed` and
`/velocity` to velocity, `/effort` to effort. Legacy `/torque` on STS3215/stepper
maps to enable/disable presets; on TI demo it maps to native effort. `/dc` remains
unsupported by runtime motor drivers. The standalone Pybricks tool defines
native effort as duty percent and requires it to be sent alone.

Normalized scalar positions map through configured operational limits and clear
the normalization flag before driver execution. SI commands cannot be normalized.
Header metadata does not imply scheduling, clock synchronization, or transforms.
