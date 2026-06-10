# Getting Started

This guide covers installation, your first run, and common entry points. For build scripts and cross-platform builds, see [scripts/README.md](../scripts/README.md).

## Prerequisites

Joshua supports **Ubuntu Linux** only. macOS is not supported (native or via Docker).

- **Docker path:** Ubuntu 22.04 or 24.04 LTS, with Docker installed (see [Docker development environment](#docker-development-environment)).
- **Native path:** Ubuntu 22.04 LTS (see [Native installation](#native-installation)).

## Docker development environment

### Linux: GPG credentials (Docker Desktop)

On Linux, Docker Desktop may require GPG key credentials. See [Docker credentials management for Linux](https://docs.docker.com/desktop/setup/sign-in/#credentials-management-for-linux-users).

### Build images

Pick the image that matches your host OS and target ROS distribution:

| Host | ROS | Build command |
|------|-----|---------------|
| Ubuntu (x86_64) | Humble (22.04) | `docker compose build joshua-u22` |
| Ubuntu (ARM64) | Humble (22.04) | `docker compose --profile arm64 build joshua-u22-arm64` |
| Ubuntu (x86_64) | Jazzy (24.04) | `docker compose --profile u24 build joshua-u24` |

The ARM64 and Jazzy services are gated behind Compose profiles (`arm64`, `u24`), so the `--profile` flag is required when building them. An ARM64 variant exists for `joshua-u24` as well (e.g. Jetson): `docker compose --profile arm64 build joshua-u24-arm64`.

### Run an interactive shell

```bash
# Ubuntu 22.04 + ROS 2 Humble
docker compose run joshua-u22

# Ubuntu 24.04 + ROS 2 Jazzy
docker compose run joshua-u24
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
bazel run //launcher:joshua_main -- --config config/config_preset/so100/teleoperate.pbtxt
```

## Example: SO100 teleoperation

This preset runs SO100 in teleoperation mode (follower and lead arm per config):

- Config: [`config/config_preset/so100/teleoperate.pbtxt`](../config/config_preset/so100/teleoperate.pbtxt)

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
| `so100/teleoperate.pbtxt` | Hardware | SO100 teleoperation |
| `so100/sim_interactive.pbtxt` | MuJoCo | SO-ARM100 interactive sim |

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

The UI service is gated behind the `production` Compose profile:

```bash
docker compose --profile production up --build joshua-ui
docker compose --profile production up -d --build joshua-ui   # detached
docker compose logs -f joshua-ui
docker compose --profile production down
```

**Development** (UI with hot reload and Zenoh bridge; add `ros2-demo-nodes` for demo traffic):

```bash
docker compose -f docker-compose.yml -f docker-compose.dev.yml up zenoh-bridge-ros2dds joshua-ui-dev
```

Do not start `joshua-ui` and `joshua-ui-dev` at the same time (both use port 3000).

Open [http://localhost:3000](http://localhost:3000).

The image builds the React app, generates protobuf schema from repo protos, and serves via nginx. Build context is the repo root so schemas can read `config/`, `robot/`, and `ai/`.

For local npm development, see [ui/README.md](../ui/README.md).

## Web control panel

See [ui/README.md](../ui/README.md) for local npm development and UI features.
