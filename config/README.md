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

## JoshuaWire serial protocol selection

JoshuaWire (JW) `0.0.2` is the sole Joshua protocol. Omitted `protocol`
selects JW for AM243, Teensy 4.1 and ESP32 serial boards. Prefer explicit fields
inside the `Board` entry:

```text
protocol: JOSHUA_WIRE
firmware { min_proto_version: 2 }
```

The semantic release version is `0.0.2`; `min_proto_version` checks the on-wire
revision, which remains `2`. JW1 support is removed. Rename explicit
`JOSHUA_WIRE_V2` configs to `JOSHUA_WIRE` and update old minimums to `2`.
JW initializes with a fresh session reset, then
IDENTIFY and CONFIGURE_CHANNEL; initialization leaves channels disabled.
Feetech does not accept this selection. The AM243 TI-demo host path is retired.
See [firmware build instructions](../firmware/README.md#joshuawire-serial).

### Serial timing

Serial timing belongs in `comm.serial_config`, independently of board identity:

```text
serial_config {
  port: "/dev/ttyUSB0"
  baudrate: 115200
  exchange_timeout_ms: 100
  post_open_settle_ms: 2000
}
```

The framed JoshuaWire exchange timeout includes waiting for the shared bus lock,
writing and receiving a complete frame. Omitted means 100 ms; explicit values
must be 1..INT_MAX milliseconds. Fixed-length vendor operations retain their
existing deadlines. Settle delay is 0..INT_MAX milliseconds, defaults to zero,
and runs once after physical open, not per request or session reset. All users
of one port must agree on baudrate and timing; omitted timeout and explicit
100 ms are equivalent. Invalid/conflicting settings fail before another open.

**ESP32 config migration:** the board-specific 2-second sleep has been removed.
For a USB bridge that resets the MCU on open, set `post_open_settle_ms: 2000`
as in the checked-in ESP32 example. Native USB or other links may need a
different value; board type no longer guesses it. The manual JW probe's
`--settle_ms` remains an additional diagnostic wait, not a runtime setting.

## JoshuaWire over EtherCAT

Select `protocol: JOSHUA_WIRE` and `transport_type: MESSAGE_AND_CYCLIC`.
The paired endpoint supplies CoE management plus correlated PDO target/feedback;
`CYCLIC` alone is rejected; the legacy TI-demo host path is retired. Endpoint facts belong in
`comm.ethercat_config`; do not add `am243_config` to a JW board.

The complete [AM243 JW EtherCAT example](config_preset/example/am243_jw_ethercat_demo.pbtxt)
configures one software-only STEP_DIR channel and a Float32 position-command
subscriber on `am243_jw_joint_1/position`. Replace `ethercat0` with the intended
NIC and confirm `slave_index` before running: this preset opens a real bus.
It requires the separate `jw` EtherCAT firmware; the default
UART/TI echo image is incompatible. The current AM243 channel does not drive
STEP/DIR GPIOs. Its timing values are illustrative and require qualification
on the intended hardware.

All seven timing fields are required, positive and at most `INT_MAX` microseconds.
The cycle must exceed `process timeout + max(state timeout, mailbox-step budget)
+ scheduling guard`. Operation/response timeouts must exceed one cycle. Runtime
mailbox transfers span multiple cycles; these checks establish budget consistency,
not hard-real-time feasibility. `response_timeout_us` bounds each adapter exchange,
including its queue wait. It is not a firmware watchdog setting.

For the software-only bring-up example, build the separate
[AM243 EtherCAT artifact](../firmware/am243/joshua_dual_transport/README.md#opt-in-jw-ethercat-profile)
with deliberately explicit watchdog intervals, e.g. command progress 2000000 µs
and target freshness 1000000 µs as used by the native integration test. These are
not motor-safety recommendations. Watchdog intervals are not advertised in the
current descriptor; confirm the flashed artifact's settings and account for
management latency before enabling. The host does not replay old targets to
feed watchdogs: fresh SET_TARGET calls are required while enabled.

An optional `pdo_region` contains all four `output_offset_bytes`,
`input_offset_bytes`, `output_size_bytes`, `input_size_bytes` values. It is an
exact assertion against discovery, not permission to reinterpret another slave's
bytes. JW requires 80-byte input/output regions. Without it, discovery supplies
offsets. Legacy `am243_config`, `am243_ethercat_config` and `MOTOR_TI_DEMO`
are rejected with migration errors. Their protobuf names/numbers remain allocated
for diagnostics; they are not executable compatibility paths. The old TI-demo
preset was removed, not silently converted to a different firmware protocol.

Every discovered slave must pass the JW descriptor/mapping check before the bus
enters OP, including unused slaves (which remain on stop images). Duplicate board
claims on a slave and mixed timing/protocol policies on a NIC are rejected.
Consumers of one NIC must run in one node process. The factory caches one master;
board teardown releases only its endpoint, and the last lease closes the bus.

The example timing budgets are illustrative, not a qualified production policy.
Account for the [master-side NIC timing limitation](../robot/comm/ethercat/README.md#known-master-side-nic-timing-issue)
and qualify deadlines on the intended host and slave hardware.
Initialization does not flash firmware. The preset is covered by parsing and
semantic validation tests, which do not open hardware.

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

One serial bus must belong to one node process. Position sensors use
`POSITION_PUBLISHER`; `ACTUATOR_SUBSCRIBER` only executes action commands and
does not read or publish sensors. Reading sensors from an actuator's bus in a
separate process is not supported by the current bus ownership model.
The `teleoperate` preset reads a separate `leader_bus` on `/dev/ttyACM1`, while
the follower actuators use `/dev/ttyACM0`. Its leader publishers and follower
subscribers share `sts3215_servo_<joint>/position` topics, carrying native
position values as `Float32` commands directly between the two nodes.
Preset tests validate declared dependencies and serial-port ownership without
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


Position publishers and actuator subscribers select compiled ROS message types
using the existing `ros2_data_type` field. No field mappings are required.
See [typed ROS messages](../ros2/README.md#typed-position-and-actuator-messages)
for supported types, fixed conversion rules, JointState units and names, and
hardware/model constraints. Unsupported message/driver combinations are rejected
before hardware initialization; existing Float32 presets remain compatible.

## Actuator motion packets

Trajectory actions use `action { joint { joint_name: "sts_motor_1" position: 2004 } }`.
`JointCommand` replaces scalar and complex action packets. Native units remain
the default, preserving existing config limits and numeric topic values. See
[packet contracts](../ros2/utils/packet_parser.md) for SI units, driver capability
limits, torque-enable presets, and migration details.

ROS endpoints contain only their topic string and `ros2_data_type`. The selected
node defines the interpretation: actuator scalar position topics use native units,
while JointState uses SI and selects the configured actuator by message name.
Position encoding remains inside JointCommand for internal consumers. Scalar
producers must convert normalized outputs before publishing.
