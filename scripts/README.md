# Scripts

Joshua’s public entrypoints are Docker-first Make targets from the repo root. Scripts in this directory support those targets.

## `setup.sh`

Host bootstrap for Docker only:

```bash
sudo ./scripts/setup.sh
```

It installs/checks Docker Engine, Docker Compose v2, buildx, and docker group membership. It does not install ROS2, Bazel, Python packages, OpenCV, CuDNN, or project runtime dependencies on the host.

## `build.py`

Implementation detail behind `make build`. It runs Bazel inside the matching Docker service and copies artifacts to `dist/<os>/<cpu>/`.

```bash
make build OS=u22 CPU=x86 TARGET=//launcher:joshua_main_pkg
make build OS=u24 CPU=arm64 TARGET=//launcher:joshua_main_pkg
```

## `container_build.sh`

Internal script used inside Docker by `build.py`. It runs `bazel build`, finds output artifacts with `bazel cquery`, copies them to `dist/`, and fixes ownership for the mounted workspace.

## Development Helpers

`docker_entrypoint.sh` creates mock serial ports when `ENABLE_MOCK_SERIAL_PORTS=true`, then starts the requested command or shell.

`create_mock_serial_ports.sh` creates PTY pairs with `socat` that emulate `/dev/ttyACM0`, `/dev/ttyACM1`, and `/dev/ttyUSB0`. Use it from inside the relevant Docker service when testing without physical serial devices.

`pybricks_spike_bridge.py` runs on a LEGO SPIKE Prime hub and bridges line-based motor commands over stdin/stdout.

`spike_wave_publisher.py` is a ROS2 test publisher. Run it through Docker:

```bash
docker compose run --rm joshua-u22 python3 scripts/spike_wave_publisher.py --topic spike/motor_A/command --hz 20
```
