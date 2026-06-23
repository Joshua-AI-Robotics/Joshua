# Getting Started

Joshua is Docker-first. The host machine should provide Docker Engine and Docker Compose v2; ROS2, Bazel, Python dependencies, OpenCV, and build tools live inside the Docker images.

## Host Bootstrap

Use the bootstrap script only to prepare Docker host tooling:

```bash
sudo ./scripts/setup.sh
make help
```

If the script adds your user to the `docker` group, log out and back in before running Docker without `sudo`.

## Docker Environments

Both supported ROS stacks are first-class:

| Stack | Command |
|------|---------|
| Ubuntu 22.04 / ROS2 Humble / Python 3.10 | `make shell-u22` |
| Ubuntu 24.04 / ROS2 Jazzy / Python 3.12 | `make shell-u24` |

Raw Docker Compose services remain available for advanced use, but public workflows should use the Make targets.

## Launcher

Run the default launcher through Docker:

```bash
make run-u22
make run-u24
```

Pass a preset config when needed:

```bash
make run-u22 CONFIG=config/config_preset/so100/teleoperate.pbtxt
make run-u24 CONFIG=config/config_preset/ant/ant_sim_interactive.pbtxt
```

Hardware runs use the privileged/device access already configured in Docker Compose. Connect the robot devices to the host before starting the container.

## Tests and Builds

Run tests inside Docker:

```bash
make test-u22
make test-u24
```

Build deployable packages inside Docker:

```bash
make build OS=u22 CPU=x86 TARGET=//launcher:joshua_main_pkg
make build OS=u24 CPU=arm64 TARGET=//launcher:joshua_main_pkg
```

Artifacts are written under `dist/<os>/<cpu>/`.

## Simulation

MuJoCo simulation runs through the same Docker launcher:

```bash
make run-u22 CONFIG=config/config_preset/so100/sim_interactive.pbtxt
make run-u24 CONFIG=config/config_preset/ant/ant_sim_interactive.pbtxt
```

Isaac Sim is not fully containerized in this repo. Launch Joshua through Docker, but provide Isaac Lab as an external GPU dependency via mounted host paths and environment variables.

```bash
export ISAAC_LAB_PATH=$HOME/IsaacLab
export ISAAC_LAB_PYTHON=$HOME/env_isaaclab/bin/python
make run-isaac-u24 CONFIG=config/config_preset/ant/ant_sim_isaac.pbtxt
```

## Web UI

Run the production UI:

```bash
make ui
```

Run the development UI with the Zenoh bridge:

```bash
make ui-dev
```

Open `http://localhost:3000`.

Stop Docker Compose services:

```bash
make down
```
