# Getting Started

Joshua is Docker-first. The host machine should provide Docker Engine and Docker Compose v2; ROS2, Bazel, Python dependencies, OpenCV, and build tools live inside the Docker images.

## Host Bootstrap

Use the bootstrap script only to prepare Docker host tooling:

```bash
sudo ./scripts/setup.sh
docker compose version
```

If the script adds your user to the `docker` group, log out and back in before running Docker without `sudo`.

## Docker Environments

Both supported ROS stacks are first-class:

| Stack | Command |
|------|---------|
| Ubuntu 22.04 / ROS2 Humble / Python 3.10 | `docker compose run --rm joshua-u22` |
| Ubuntu 24.04 / ROS2 Jazzy / Python 3.12 | `docker compose run --rm joshua-u24` |

Docker Compose is the canonical interface. The optional Makefile provides shorter aliases; run `make help` to list them.

## Launcher

Run the default launcher through Docker:

```bash
docker compose run --rm run-u22
docker compose run --rm run-u24
```

Pass a preset config when needed:

```bash
CONFIG=config/config_preset/so100/teleoperate.pbtxt docker compose run --rm run-u22
CONFIG=config/config_preset/ant/ant_sim_interactive.pbtxt docker compose run --rm run-u24
```

Hardware runs use the privileged/device access already configured in Docker Compose. Connect the robot devices to the host before starting the container.

## Tests and Builds

Run tests inside Docker:

```bash
docker compose run --rm test-u22
docker compose run --rm test-u24
```

Build deployable packages inside Docker:

```bash
TARGET=//launcher:joshua_main_pkg docker compose run --rm build-u22-x86
TARGET=//launcher:joshua_main_pkg docker compose run --rm build-u24-arm64
```

Artifacts are written under `dist/<os>/<cpu>/`.

## Simulation

MuJoCo simulation runs through the same Docker launcher:

```bash
CONFIG=config/config_preset/so100/sim_interactive.pbtxt docker compose run --rm run-u22
CONFIG=config/config_preset/ant/ant_sim_interactive.pbtxt docker compose run --rm run-u24
```

Isaac Sim is not fully containerized in this repo. Launch Joshua through Docker, but provide Isaac Lab as an external GPU dependency via mounted host paths and environment variables.

```bash
export ISAAC_LAB_PATH=$HOME/IsaacLab
export ISAAC_LAB_PYTHON=$HOME/env_isaaclab/bin/python
CONFIG=config/config_preset/ant/ant_sim_isaac.pbtxt \
  docker compose -f docker-compose.yml -f docker-compose.isaac.yml run --rm run-u24
```

## Web UI

Run the production UI:

```bash
docker compose --profile production up --build joshua-ui
```

Run the development UI with the Zenoh bridge:

```bash
docker compose -f docker-compose.yml -f docker-compose.dev.yml \
  up zenoh-bridge-ros2dds joshua-ui-dev
```

Open `http://localhost:3000`.

Stop Docker Compose services:

```bash
docker compose -f docker-compose.yml -f docker-compose.dev.yml \
  --profile production --profile u24 --profile arm64 --profile mac down
```
