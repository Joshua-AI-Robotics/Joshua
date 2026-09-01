# Getting Started

Joshua is Docker-first and GPU-first for development. The host should provide Docker Engine, Docker Compose v2, an NVIDIA driver, and NVIDIA Container Toolkit. CPU-only development uses an explicit fallback. ROS2, Bazel, Python dependencies, CUDA user-space libraries, OpenCV, and build tools live inside Docker.

## Host Bootstrap

Use the bootstrap script to prepare Docker and NVIDIA host tooling:

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

**After pulling changes** that touch `dockerfiles/`, `docker/`, or the requirements lockfiles, rebuild before running tasks — `docker compose run` does not rebuild on older Compose versions, and a stale image can be missing fixes that live in the image itself:

```bash
docker compose build joshua-u22   # and joshua-u24 if you use it
```

### Keep a development container running

For repeated builds and tests, prefer a persistent container over one-off
`docker compose run --rm` commands. This keeps the Bazel server and cache warm
and avoids bind-mount ownership churn from short-lived containers.

```bash
# Start Ubuntu 24.04 + ROS 2 Jazzy once.
docker compose --profile u24 up -d joshua-u24

# Run commands in the existing container (Bazel needs the matching configs).
docker compose exec joshua-u24 bazel test --config=u24 --config=x86-base \
  --@rules_python//python/config_settings:python_version=3.12 \
  //robot/board/am243:am243_board_test

# Open an interactive shell in the existing container.
docker compose exec joshua-u24 bash
```

## Launcher

Run the default launcher through Docker:

```bash
docker compose run --rm run-u22
docker compose run --rm run-u24
```

Pass a preset config when needed:

```bash
CONFIG=config/config_preset/so100/teleoperate.pbtxt docker compose run --rm run-u22
CONFIG=config/config_preset/so100/sim_interactive.pbtxt docker compose run --rm run-u24
```

| Config | Backend | What it does |
|--------|---------|-------------|
| `so100/sim_interactive.pbtxt` | MuJoCo | SO-ARM100 interactive 3D viewer |
| `so100/sim_passive.pbtxt` | MuJoCo | SO-ARM100 passive sim |
| `so100/sim_mirror.pbtxt` | MuJoCo | Sim mirrors a real arm — **opens `/dev/ttyACM1`** |
| `so100/teleoperate.pbtxt` | Hardware | SO100 teleoperation |

Hardware runs use the privileged/device access already configured in Docker Compose. Connect the robot devices to the host before starting the container.

Set each actuator's `operational_lower_limit` / `operational_upper_limit` in
the preset before running hardware. (The `MODE_CALIBRATION` node that measured
these automatically was removed; set them by hand.)

## GPU and CPU modes

Developer shells and launcher tasks request an NVIDIA GPU by default. Setup validates the host driver and installs NVIDIA Container Toolkit without installing project dependencies or the host CUDA toolkit:

```bash
sudo ./scripts/setup.sh
docker compose run --rm joshua-u22 nvidia-smi
```

Run AI inference with the normal launcher service:

```bash
CONFIG=config/config_preset/so100/random_noise.pbtxt docker compose run --rm run-u22
```

For CPU-only development, skip NVIDIA setup and apply the CPU override:

```bash
sudo ./scripts/setup.sh --cpu
docker compose -f docker-compose.yml -f docker-compose.cpu.yml \
  run --rm joshua-u22
```

**Isaac Sim viewer** (requires Isaac Lab installed): the Isaac Sim backend
(`SIM_BACKEND_ISAAC_SIM`) is supported by `simulation/isaac/`, but **no preset
ships with it** — the ant/trileg/bileg Isaac presets were removed. Write one
against [simulation/README.md](../simulation/README.md) to use it.

Tests, package builds, UI services, and CI remain GPU-independent. The host owns the NVIDIA driver and Container Toolkit; CUDA-enabled Python packages are installed inside model-specific environments.

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
CONFIG=config/config_preset/so100/sim_passive.pbtxt docker compose run --rm run-u24
```

Isaac Sim is not fully containerized in this repo. Launch Joshua through Docker, but provide Isaac Lab as an external GPU dependency via mounted host paths and environment variables. **No Isaac preset ships with the repo** — write one against [simulation/README.md](../simulation/README.md) first.

```bash
export ISAAC_LAB_PATH=$HOME/IsaacLab
export ISAAC_LAB_PYTHON=$HOME/env_isaaclab/bin/python
CONFIG=<your-isaac-preset>.pbtxt \
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
