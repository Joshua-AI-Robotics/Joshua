# Architecture

Joshua is a config-driven robotics stack: one protobuf text config defines hardware, AI policy, and operation mode; the launcher builds and runs the matching ROS 2 nodes. Monitoring and control are available through the Qt6 and React control panels.

![Project Joshua core concept](../assets/images/project_joshua_diagram_napkin.png)

A representative preset: [`config/config_preset/so100/so100_teleoperate.pbtxt`](../config/config_preset/so100/so100_teleoperate.pbtxt).

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

- Simple: position, torque, speed
- Presets: middle position, idle, teardown
- Complex multi-parameter actions

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

## Related documentation

- [Getting started](GETTING_STARTED.md) — install and first run
- [ai/train/README.md](../ai/train/README.md) — data collection, training, sim backends
- [ros2/README.md](../ros2/README.md) — ROS 2 type resolver utilities
- [node_generator/README.md](../node_generator/README.md) — node generation from config
