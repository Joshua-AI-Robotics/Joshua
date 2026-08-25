# Scripts

Joshua's public entrypoints are Docker Compose services from the repo root. The Makefile provides optional shorthand.

## `setup.sh`

Host bootstrap for Docker only:

```bash
sudo ./scripts/setup.sh
```

It installs/checks Docker Engine, Docker Compose v2, buildx, docker group membership, and NVIDIA Container Toolkit. It requires an existing NVIDIA driver, but does not install the host CUDA toolkit, ROS2, Bazel, Python packages, OpenCV, CuDNN, or project runtime dependencies.

Verify the default GPU-enabled developer shell:

```bash
sudo ./scripts/setup.sh
docker compose run --rm joshua-u22 nvidia-smi
```

For CPU-only development:

```bash
sudo ./scripts/setup.sh --cpu
docker compose -f docker-compose.yml -f docker-compose.cpu.yml \
  run --rm joshua-u22
```

Default setup validates the host NVIDIA driver, installs NVIDIA Container Toolkit, and configures Docker. CUDA-enabled Python dependencies stay inside model environments.

## `container_build.sh`

Internal script used by the `build-<os>-<cpu>` Compose services. It runs `bazel build`, finds output artifacts with `bazel cquery`, copies them to `dist/`, and fixes ownership for the mounted workspace.

```bash
TARGET=//launcher:joshua_main_pkg docker compose run --rm build-u22-x86
TARGET=//launcher:joshua_main_pkg docker compose run --rm build-u24-arm64
```

## Development Helpers

The container entrypoint ([docker/entrypoint.sh](../docker/entrypoint.sh), baked into the images) sources the ROS2 environment for every container command, creates mock serial ports when `ENABLE_MOCK_SERIAL_PORTS=true`, then starts the requested command or shell.

`create_mock_serial_ports.sh` creates PTY pairs with `socat` that emulate `/dev/ttyACM0`, `/dev/ttyACM1`, and `/dev/ttyUSB0`. Use it from inside the relevant Docker service when testing without physical serial devices.

`pybricks_spike_bridge.py` runs on a LEGO SPIKE Prime hub and bridges line-based motor commands over stdin/stdout. Deploy it via the Pybricks app or `pybricksdev`. The host-side driver that drove it now lives in [tools/pybricks/](../tools/README.md); Joshua has no board type for a SPIKE hub, so the launcher cannot reach one. This script is kept for manual hub-side testing.

`spike_wave_publisher.py` is a ROS2 test publisher. Run it through Docker:

```bash
docker compose run --rm joshua-u22 python3 scripts/spike_wave_publisher.py --topic spike/motor_A/command --hz 20
```
