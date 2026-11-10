# Architecture

Joshua is a config-driven robotics stack: one protobuf text config defines hardware, AI policy, and operation mode; the launcher builds and runs the matching ROS 2 nodes. Monitoring and control are available through the React web control panel.

![Project Joshua core concept](../assets/images/joshua_summary.png)

A representative preset: [`config/config_preset/so100/teleoperate.pbtxt`](../config/config_preset/so100/teleoperate.pbtxt).

## Configuration layer (Protobuf)

All static configuration uses Protocol Buffers:

| Proto | Role |
|-------|------|
| `config.proto` | Top-level structure: `General`, `Robot`, `Ai` |
| `robot.proto` | Hardware: actions and perceptions |
| `ai.proto` | AI policies and model parameters |
| `action.proto` | Actuators, interfaces, operational parameters |
| `perception.proto` | Cameras, encoders, and other sensors |

Presets live under `config/config_preset/`. The launcher reads a `.pbtxt` file and instantiates the corresponding node graph.

## Runtime data layer (Protobuf + ROS 2)

At runtime Joshua uses protobuf packets internally and standard ROS 2 messages at the graph boundary.

### Internal packets

**`action_packet.proto`** — action commands:

- `joint`: named motion command with optional position, velocity, effort, explicit position encoding, independent velocity/effort units, and source metadata
- `preset`: middle position, idle, teardown, and torque enable/disable

**`perception_packet.proto`** — unified perception:

- Images (dimensions, encoding, raw bytes)
- Position (position, velocity)
- Generic sensor arrays with labels
- Point clouds (reserved for LiDAR/radar)

### Data flow

```
Hardware Interface → Protobuf Packets → ROS 2 Publishers → ROS 2 Messages
      → ROS 2 Subscribers → Protobuf Packets → Hardware Interface
```

### Design goals

- **Type safety** — protobuf at hardware boundaries
- **Performance** — efficient serialize/deserialize on hot paths
- **Compatibility** — ROS 2 types for ecosystem tools
- **Extensibility** — new sensors and action types without breaking the core
- **Modularity** — clear split between config, runtime packets, and ROS 2 topics

## Process runtime

The C++ `JOSHUA_MAIN` entrypoint macro delegates to a shared exception guard.
`JoshuaInit(config)` installs process services, starting with RobotTime; it does
not install a catch that survives an ordinary function return. ROS adapters own
their executor and node lifetime, and worker/destructor boundaries report
failures through the ROS-independent runtime utility.

The launcher owns one `NodeManager` in `launcher/node_manager/node_manager.h/.cc`.
It loads the config, resolves executables, and owns each job's guardian,
application PID, process group, readiness channels, exit history, and shutdown
sequence. ROS nodes and simulation share that manager and one registry of plain
node records. Robot drivers and ROS objects remain inside their application
process. See the [node manager guide](../launcher/node_manager/README.md) for the
Linux ownership contract and [ROS runtime guide](../ros2/README.md#process-runtime-and-failure-handling)
for startup, cancellation, and cleanup.

## Policy execution

The inference host owns ROS communication and scheduling; model plug-ins own
preprocessing, inference, and action decoding. The protobuf preset selects the
plug-in and its wiring. MuJoCo and Isaac Sim provide separate simulation backends.

The proposed [policy interface and bridges plan](POLICY_INTERFACE_AND_BRIDGES_PLAN.md)
extends this boundary with named observations and commands shared across
simulation and hardware. Backend bridges handle state and command mapping;
model logic stays in plug-ins and training remains external. The first priority
is the OpenAI Decisions API camera-head adapter and its minimum asynchronous
observation/command contract, using the qualified shared head runtime from
[milestone #153](https://github.com/Joshua-AI-Robotics/Joshua/issues/153).
Broader local-model and simulator integration follows later. The adapter and
shared execution bridges are planned, not yet implemented on this branch.

## Hardware topology and latency budgets

For high-bandwidth sensing or tight actuator deadlines, prefer separate actuator
and sensor-acquisition boards with independent physical links. Shared boards/links
remain supported when measurements show sufficient bandwidth and latency headroom
under the intended worst-case workload. Keep feedback needed by the local motor
control loop, such as encoder/current measurements, on the actuator controller.

Queues and priorities manage contention but do not guarantee deadlines. Separate
ROS nodes do not isolate a shared link, and separate boards may still contend for
USB-controller or host resources. Qualify either arrangement against its command,
stop and sensor-loss budgets; see the
[topology comparison checklist](SERIAL_COMM_QUALIFICATION.md#hardware-topology-comparison-todo)
for measurement requirements and outstanding work.

## Related documentation

- [Getting started](GETTING_STARTED.md) — install and first run
- [ai/README.md](../ai/README.md) — inference engine, model plug-ins, per-model venvs
- [ai/train/README.md](../ai/train/README.md) — data collection (DataStore)
- [simulation/README.md](../simulation/README.md) — MuJoCo and Isaac Sim backends
- [ros2/README.md](../ros2/README.md) — ROS 2 type resolver utilities
- [launcher/node_manager/README.md](../launcher/node_manager/README.md) — node lifecycle and process ownership
