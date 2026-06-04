# Getting Started

This guide covers installation, your first run, and common entry points. For build scripts and cross-platform builds, see [scripts/README.md](../scripts/README.md).

## Prerequisites

- **Docker path:** Ubuntu or macOS (Apple Silicon), with [Docker Desktop](https://www.docker.com/products/docker-desktop/) installed and running.
- **Native path:** Ubuntu 22.04 LTS (see [Native installation](#native-installation)).

## Docker development environment

### Linux: GPG credentials (Docker Desktop)

On Linux, Docker Desktop may require GPG key credentials. See [Docker credentials management for Linux](https://docs.docker.com/desktop/setup/sign-in/#credentials-management-for-linux-users).

### Build images

Pick the image that matches your host OS and target ROS distribution:

| Host | ROS | Build command |
|------|-----|---------------|
| Linux | Humble (22.04) | `docker compose build joshua-u22` |
| Linux (ARM64) | Humble (22.04) | `docker compose build joshua-u22-arm64` |
| Linux | Jazzy (24.04) | `docker compose build joshua-u24` |
| macOS Apple Silicon | Humble (22.04, arm64) | `docker compose build joshua-mac-u22-arm64` |

ARM64 variants exist for `joshua-u24` as well.

**macOS notes:** Docker images for Mac support arm64 builds only. Serial ports are mocked by default; for real serial ports, see comments in `docker-compose.yml`.

### Run an interactive shell

```bash
# Ubuntu 22.04 + ROS 2 Humble
docker compose run joshua-u22

# Ubuntu 24.04 + ROS 2 Jazzy
docker compose run joshua-u24

# macOS Apple Silicon
docker compose run joshua-mac-u22-arm64
```

Type `exit` to leave the shell. To resume a stopped container:

```bash
docker container list -a
docker start -ai <container_name>
```

Example:

```bash
docker start -ai joshua-joshua-u22-run-a199afce4b8a
```

## Native installation

On Ubuntu 22.04 LTS, install dependencies with the setup script:

```bash
sudo ./scripts/setup.sh --env=dev
```

See [scripts/README.md](../scripts/README.md) for `runtime` mode, build helpers, and Ubuntu 24.04 / Jazzy support.

## First run: launcher

From the repo root (inside Docker or natively), start the main launcher:

```bash
bazel run launcher:joshua_main
```

Pass a preset config when needed:

```bash
bazel run //launcher:joshua_main -- --config config/config_preset/so100/so100_teleoperate.pbtxt
```

## Example: SO100 teleoperation

This preset runs SO100 in teleoperation mode (follower and lead arm per config):

- Config: [`config/config_preset/so100/so100_teleoperate.pbtxt`](../config/config_preset/so100/so100_teleoperate.pbtxt)

Calibrate operational limits for your servo motors before running.

```bash
bazel run launcher:joshua_main
```

## Simulation and RL (overview)

All modes use `joshua_main`; the preset’s `operation_mode` selects behavior. Full pipeline docs: [ai/train/README.md](../ai/train/README.md).

**MuJoCo interactive viewer:**

```bash
bazel run //launcher:joshua_main -- --config config/config_preset/ant/ant_sim_interactive.pbtxt
```

**MJX training (GPU, JAX):**

```bash
bazel run //launcher:joshua_main -- --config config/config_preset/ant/ant_train_mjx.pbtxt
```

**Isaac Lab** (requires Isaac Lab installed):

```bash
export ISAAC_LAB_PATH=~/IsaacLab
export ISAAC_LAB_PYTHON=~/env_isaaclab/bin/python

bazel run //launcher:joshua_main -- --config config/config_preset/ant/ant_train_isaac_full_skrl.pbtxt
```

### Preset reference

| Config | Backend | What it does |
|--------|---------|-------------|
| `ant/ant_sim_interactive.pbtxt` | MuJoCo | Interactive 3D viewer |
| `ant/ant_train_isaac_full_skrl.pbtxt` | Isaac Sim | Train Ant (skrl PPO) |
| `ant/ant_train_isaac_full_rsl_rl.pbtxt` | Isaac Sim | Train Ant (RSL-RL PPO) |
| `ant/ant_eval_isaac_full_skrl.pbtxt` | Isaac Sim | Evaluate Ant (skrl) |
| `ant/ant_eval_isaac_full_rsl_rl.pbtxt` | Isaac Sim | Evaluate Ant (RSL-RL) |
| `trileg/trileg_train_isaac_full_skrl.pbtxt` | Isaac Sim | Train 3-legged robot (skrl) |
| `trileg/trileg_train_isaac_full_rsl_rl.pbtxt` | Isaac Sim | Train 3-legged robot (RSL-RL) |
| `trileg/trileg_eval_isaac_full_skrl.pbtxt` | Isaac Sim | Evaluate 3-legged robot (skrl) |
| `trileg/trileg_eval_isaac_full_rsl_rl.pbtxt` | Isaac Sim | Evaluate 3-legged robot (RSL-RL) |
| `so100/so100_teleoperate.pbtxt` | Hardware | SO100 teleoperation |
| `so100/so_arm100_sim_interactive.pbtxt` | MuJoCo | SO-ARM100 interactive sim |

## Web UI with Docker

The React control panel can be built and served via Docker Compose from the project root.

### Prerequisites

- Docker and **Docker Compose v2** (`docker compose`, not `docker-compose`):

```bash
sudo apt install docker.io docker-compose-plugin
sudo usermod -aG docker $USER
newgrp docker
docker compose version
```

### Build and run

```bash
docker compose up --build
docker compose up -d --build   # detached
docker compose logs -f
docker compose down
```

**Development** (Zenoh bridge and demo nodes):

```bash
docker compose -f docker-compose.yml -f docker-compose.dev.yml up
```

Open [http://localhost:3000](http://localhost:3000).

The image builds the React app, generates protobuf schema from repo protos, and serves via nginx. Build context is the repo root so schemas can read `config/`, `robot/`, and `ai/`.

For local npm development, see [ui/README.md](../ui/README.md).

## Control panels

| UI | Guide |
|----|--------|
| Qt6 desktop panel | [joshua_control_panel/README.md](../joshua_control_panel/README.md) |
| React web panel | [ui/README.md](../ui/README.md) |
