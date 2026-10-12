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

## ROS 2 discovery across computers

Set `general.ros2_network` to configure discovery before Joshua initializes ROS.
For example, on the computer that needs to discover a peer at `192.0.2.20`:

```protobuf
general {
  # Keep the preset's existing operation_mode and other settings.
  ros2_network {
    domain_id: 0
    discovery: STATIC_PEERS
    peers: "192.0.2.20"
    participant_scan_count: 32
  }
}
```

Use the other computer's actual LAN address in each preset. Each computer runs
its own preset and local nodes. Reserve addresses in DHCP so peer addresses stay
stable. Both sides need the same domain, compatible topic types and QoS, and
reachable UDP ports. This config does not deploy processes to another computer.

| Setting | Behavior |
|---|---|
| Block omitted / `INHERIT` | Preserve existing discovery environment. An explicitly supplied `domain_id` still overrides `ROS_DOMAIN_ID`. |
| `AUTOMATIC` | Fast DDS discovery on the local subnet; requires multicast between computers. |
| `LOCALHOST` | Fast DDS discovery restricted to this computer. |
| `STATIC_PEERS` | Fast DDS unicast discovery to the configured IPv4 addresses and loopback, without multicast discovery. |
| `domain_id` | Optional, 0–101. If omitted, inherit `ROS_DOMAIN_ID`, defaulting to 0. |
| `participant_scan_count` | Static peers only; default 32, range 1–120. Probe participant IDs from zero through count minus one on each host. Domain 101 allows at most 54 to avoid Linux's default ephemeral ports. |

Managed modes require `rmw_fastrtps_cpp`; `INHERIT` also permits other middleware.
Peers must be unicast IPv4 addresses. Hostnames, IPv6, and discovery servers are
not supported by managed modes. Empty peer lists and incompatible settings fail
before node construction. An unavailable peer does not block launch.

Explicit settings override the corresponding domain and localhost environment
variables. Conflicting `RMW_IMPLEMENTATION`, `ROS_DISCOVERY_SERVER`,
`ROS_SUPER_CLIENT`, `ROS_STATIC_PEERS`, or DDS profile files produce an error
asking you to unset the variable or select `INHERIT`. Identical inherited Joshua
profiles are reused. Generated profiles live until their owning launcher or
standalone node exits. Standalone C++/Python nodes and simulation use the same
preparation component. Python's adapter calls the packaged C++ helper, so profile
generation and validation have one implementation.

Participant IDs belong to DDS processes/contexts, not Joshua `node_id` values.
Include ROS CLI processes in the scan range. Ports follow Fast DDS's standard
mapping (`7410 + 250 * domain_id + 2 * participant_id` for discovery); changing
the domain or scan range regenerates the profile. The scan range determines
which remote participants can be contacted, not the maximum number of nodes
Joshua will launch.

### ROS CLI and external applications

The export command reads only networking settings from a preset. It never
constructs nodes or opens robot hardware. Inside a Docker development shell:

```bash
mkdir -p /workspace/.cache/ros-network
bazel run --config=u22 --config=x86-base \
  --@rules_python//python/config_settings:python_version=3.10 \
  //config:ros2_network_export -- \
  --config=/workspace/config/config_preset/<robot>/<preset>.pbtxt \
  --output_parent=/workspace/.cache/ros-network \
  > /workspace/.cache/ros-network/env.sh
source /workspace/.cache/ros-network/env.sh
ros2 daemon stop
ros2 topic list
```

Use `u24` and Python `3.12` for Jazzy. Source the exported file in each terminal
that starts a publisher, subscriber, or external ROS application. Restart an
existing ROS CLI daemon after changing settings; changing a terminal's environment
does not reconfigure already running nodes. Exported profiles persist until you
remove them. Re-export after changing the config, first unsetting both
`FASTRTPS_DEFAULT_PROFILES_FILE` and `FASTDDS_DEFAULT_PROFILES_FILE` if they refer
to the old settings.

Joshua's Linux Compose services use host networking. For another container,
share the exported directory at the **same absolute path** and use host
networking. Generate the export separately on each computer. Environment exports
alone cannot bypass a firewall blocking unicast traffic.
