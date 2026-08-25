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

### Keep a development container running

For repeated builds and tests, prefer a persistent container over one-off
`docker compose run --rm` commands. This keeps the Bazel server and cache warm
and avoids bind-mount ownership churn from short-lived containers.

```bash
# Start Ubuntu 24.04 + ROS 2 Jazzy once.
docker compose --profile u24 up -d joshua-u24

# Run commands in the existing container.
docker compose exec joshua-u24 bazel test //robot/board/am243:am243_board_test

# Open an interactive shell in the existing container.
docker compose exec joshua-u24 bash
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

Set each actuator's `operational_lower_limit` / `operational_upper_limit` in
the preset before running. (The `MODE_CALIBRATION` node that measured these
automatically was removed; set them by hand.)

```bash
bazel run launcher:joshua_main
```

## Simulation (overview)

All modes use `joshua_main`; the preset’s `operation_mode` selects behavior. Full simulation docs: [simulation/README.md](../simulation/README.md).

**MuJoCo interactive viewer:**

```bash
bazel run //launcher:joshua_main -- --config config/config_preset/so100/sim_interactive.pbtxt
```

**Isaac Sim viewer** (requires Isaac Lab installed): the Isaac Sim backend
(`SIM_BACKEND_ISAAC_SIM`) is supported by `simulation/isaac/`, but **no preset
ships with it** — the ant/trileg/bileg Isaac presets were removed. Write one
against [simulation/README.md](../simulation/README.md) to use it.

### Preset reference

| Config | Backend | What it does |
|--------|---------|-------------|
| `so100/sim_interactive.pbtxt` | MuJoCo | SO-ARM100 interactive 3D viewer |
| `so100/sim_passive.pbtxt` | MuJoCo | SO-ARM100 passive sim |
| `so100/sim_mirror.pbtxt` | MuJoCo | Sim mirrors a real arm — **opens `/dev/ttyACM1`** |
| `so100/teleoperate.pbtxt` | Hardware | SO100 teleoperation |

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
