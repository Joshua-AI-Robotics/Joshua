# JOSHUA (**J**oint **O**pen-**S**ource **H**ub for **U**niversal **A**utomation)

**A modular framework for robotic AI systems**

Joshua turns a single protobuf config into a running robot stack on ROS 2: hardware (actions and perceptions), AI policy, and operation mode. The launcher builds and runs the right nodes; Qt6 and React control panels let you configure, launch, and monitor the system.

![Project Joshua core concept](assets/images/project_joshua_diagram_napkin.png)

One config file is the source of truth—for example [`config/config_preset/so100/so100_teleoperate.pbtxt`](config/config_preset/so100/so100_teleoperate.pbtxt) defines the full SO100 teleop setup. See [Architecture](docs/ARCHITECTURE.md) for how config, protobuf packets, and ROS 2 fit together.

## Quick start

**Docker (recommended for first try):**

```bash
docker compose build joshua-u22
docker compose run joshua-u22
bazel run launcher:joshua_main
```

**Native Ubuntu 22.04:**

```bash
sudo ./scripts/setup.sh --env=dev
bazel run launcher:joshua_main
```

Install options (macOS, ARM64, ROS Jazzy), SO100 teleop, simulation presets, and the web UI: [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md).

## Documentation

| Topic | Guide |
|-------|--------|
| Getting started | [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) |
| Architecture | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| All docs index | [docs/README.md](docs/README.md) |
| Scripts and builds | [scripts/README.md](scripts/README.md) |
| Simulation and RL | [ai/train/README.md](ai/train/README.md) |
| Web UI | [ui/README.md](ui/README.md) |
| Qt control panel | [joshua_control_panel/README.md](joshua_control_panel/README.md) |

## License

Joshua is licensed under the [Apache License, Version 2.0](LICENSE). See
[NOTICE](NOTICE) for attribution and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
for dependencies (including Qt6 under LGPL-3.0).
