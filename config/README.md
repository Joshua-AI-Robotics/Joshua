# Config

The protobuf config is the **single source of truth** for a robot setup. One
`.pbtxt` file describes the whole system — actuators, boards, sensors, AI
policies, simulation backend — and the launcher instantiates ROS 2 nodes from
it. If a value describes a particular robot, it belongs here, not in code.

```bash
bazel run //launcher:joshua_main -- --config config/config_preset/so100/sim_passive.pbtxt
```

> A preset may drive **real hardware** whatever it is named and whatever
> `operation_mode` it declares. Read the preset before running it — see the
> hardware-safety section of [AGENTS.md](../AGENTS.md).

## Layout

- `proto/` — the schema.
  - `robot.proto` — top-level `Robot`; composes the others.
  - `config.proto` — `General`, including `operation_mode`, which the launcher
    branches on.
  - `ai.proto` — inference and policy configuration.
  - `physical_layout.proto` — poses, axes, and frames.
- `config_preset/<robot>/` — checked-in presets, one per robot and task
  (`so100/`, `example/`).
- `config_utils.h` — `LoadConfig`, which opens a `.pbtxt` and parses it into a
  `config::Config`. Parsing only; semantic checks happen later, in the
  factories and `node_generator/`.

## Responsibilities

- Describe *what* a robot is made of and how it is wired.
- Stay the only place a robot-specific value appears: ports, joint limits, gear
  ratios, servo IDs, topic names, model paths.
- Carry enough structure that an inconsistent setup can be rejected during
  startup — in the factories and `node_generator/` — rather than at first
  motion.

## Non-Goals

- Behavior. How a motor moves is driver logic in [robot/](../robot/README.md).
- Secrets or machine-local paths. Presets are committed and shared.
- Generated artifacts. Do not hand-edit anything produced by codegen.

## Adding or changing a preset

1. Start from the nearest existing preset in the same robot directory.
2. Change the schema in `proto/` only if the field genuinely does not exist —
   a new preset should rarely need one.
3. For anything you intend to run repeatedly during development, pick a preset
   you have read and confirmed declares no real devices. Neither the filename
   nor `operation_mode: MODE_SIMULATION` is that confirmation — both cross the
   safe/unsafe boundary in this repo. What counts as safe is defined once, in
   the hardware-safety section of [AGENTS.md](../AGENTS.md); check the preset
   against that list rather than against a copy of it here.

## Sensor configuration

Each `single_perceptions` entry declares `sensor_name`, `sensor_type`, and one
concrete config: `opencv_config` for `IMAGE`, `lds01_config` for `RANGE_SCAN`, or
`sts3215_encoder_config` for `POSITION`. Sensor names also identify output
perception packets. Legacy `perception_type` and `camera`/`encoder`/`lidar`
wrappers are no longer accepted.

For example, position feedback references a configured board channel:

```text
sensor_name: "joint_1"
sensor_type: POSITION
sts3215_encoder_config { board_name: "arm_bus" channel: 1 }
```

The board owns serial settings and servo IDs. Position readings retain the
channel's native units; the old encoder operational limits were unused and are
not part of the new sensor config. Actuator limits remain configured separately.
OpenCV keeps its camera index and image settings in `opencv_config`; LDS01 keeps
its transport settings in `lds01_config.comm`.

One serial bus must belong to one node process. If position sensors read the
actuator board, give them the actuator's node ID and `ACTUATOR_SUBSCRIBER` node
type; that process publishes feedback as well as accepting commands. Sensors
on a separate board can use `ENCODER_PUBLISHER`. The `smolvla` preset shares
`arm_bus` on `/dev/ttyACM0`; `teleoperate` reads a separate `leader_bus` on
`/dev/ttyACM1`. Preset tests validate declared dependencies and serial-port ownership without
opening hardware.

`config::ValidateConfig` in [validation.h](validation.h)
accepts the full `config::Config` and orchestrates separate checks for node assignments,
board/channel references, serial settings, and bus ownership. These checks use
resource dependencies rather than sensor measurement types. A sensor can
require board channels, direct communication, both, or neither. Sensor config
checks and dependency extraction are separate private helpers in that module.
Adding a driver updates those helpers; the shared resource checks
remain independent of sensor types. Factories keep defensive construction checks
for direct callers, without depending on the validation module.
There is no central sensor-to-publisher allowlist; node validation checks that
node types are specified and each node ID has one consistent type.
