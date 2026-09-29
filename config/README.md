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

Existing board configs keep their current protocol when `protocol` is omitted.
For a serial AM243, Teensy 4.1, or ESP32 running the matching **v2 firmware
artifact**, add these fields inside its `Board` entry:

```text
protocol: JOSHUA_WIRE_V2
firmware { min_proto_version: 2 }
```

This is an explicit selection, not version negotiation. V1 and v2 artifacts
reject each other's frames. V2 initializes with a fresh session reset, then
IDENTIFY and CONFIGURE_CHANNEL; initialization leaves channels disabled.
Feetech does not accept this selection. The AM243 TI-demo host path is retired.
See [firmware build instructions](../firmware/README.md#opt-in-joshuawire-v2-serial-milestone).

## JoshuaWire v2 over EtherCAT

Select `protocol: JOSHUA_WIRE_V2` and `transport_type: MESSAGE_AND_CYCLIC`.
The paired endpoint supplies CoE management plus correlated PDO target/feedback;
`CYCLIC` alone is rejected; the legacy TI-demo host path is retired. Endpoint facts belong in
`comm.ethercat_config`; do not add `am243_config` to a JW2 board.

Example board fragment (not a hardware-validated timing recommendation):

```text
name: "am243_jw2"
board_type: AM243
protocol: JOSHUA_WIRE_V2
firmware { min_proto_version: 2 }
comm {
  comm_type: ETHERCAT
  transport_type: MESSAGE_AND_CYCLIC
  ethercat_config {
    interface_name: "ethercat0"
    process_data_mode: ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR
    slave_index: 1
    timing {
      period_us: 20000
      process_timeout_us: 1000
      state_timeout_us: 1000
      operation_timeout_us: 1000000
      mailbox_step_budget_us: 1000
      scheduling_guard_us: 1000
      response_timeout_us: 1000000
    }
  }
}
channels {
  index: 0
  drive: STEP_DIR
  step_dir { max_pulse_rate_hz: 1000 step_pin: 2 dir_pin: 3 enable_pin: 4 }
}
```

All seven timing fields are required, positive and at most `INT_MAX` microseconds.
The cycle must exceed `process timeout + max(state timeout, mailbox-step budget)
+ scheduling guard`. Operation/response timeouts must exceed one cycle. Runtime
mailbox transfers span multiple cycles; these checks establish budget consistency,
not hard-real-time feasibility. `response_timeout_us` bounds each adapter exchange,
including its queue wait. It is not a firmware watchdog setting.

For the software-only bring-up example, build the separate
[AM243 EtherCAT artifact](../firmware/am243/joshua_dual_transport_v1/README.md#opt-in-jw2-ethercat-profile)
with deliberately explicit watchdog intervals, e.g. command progress 2000000 µs
and target freshness 1000000 µs as used by the native integration test. These are
not motor-safety recommendations. Watchdog intervals are not advertised in the
current descriptor; confirm the flashed artifact's settings and account for
management latency before enabling. The host does not replay old targets to
feed watchdogs: fresh SET_TARGET calls are required while enabled.

An optional `pdo_region` contains all four `output_offset_bytes`,
`input_offset_bytes`, `output_size_bytes`, `input_size_bytes` values. It is an
exact assertion against discovery, not permission to reinterpret another slave's
bytes. JW2 requires 80-byte input/output regions. Without it, discovery supplies
offsets. Legacy `am243_config`, `am243_ethercat_config` and `MOTOR_TI_DEMO`
are rejected with migration errors. Their protobuf names/numbers remain allocated
for diagnostics; they are not executable compatibility paths. The old TI-demo
preset was removed, not silently converted to a different firmware protocol.

Every discovered slave must pass the JW2 descriptor/mapping check before the bus
enters OP, including unused slaves (which remain on stop images). Duplicate board
claims on a slave and mixed timing/protocol policies on a NIC are rejected.
Consumers of one NIC must run in one node process. The factory caches one master;
board teardown releases only its endpoint, and the last lease closes the bus.

The host/factory/firmware-core path is native-tested and has a limited
[single-board EtherCAT bench result](../docs/JOSHUA_WIRE_V2_VALIDATION.md#recorded-am243-ethercat-result--2026-09-28).
Both the 1 ms timing budgets above and a temporary 5 ms policy encountered
register-datagram deadline failures on that host; do not treat this fragment as
a validated production timing policy.
No runnable preset was added and no firmware is flashed by initialization.

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
