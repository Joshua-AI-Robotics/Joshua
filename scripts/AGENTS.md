# Setup and build scripts

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../AGENTS.md#coding-guidelines). This file owns the rules and architecture
contract for `scripts/`; child directories inherit it unless they add local rules.

## Architecture contract

This directory owns host setup, container task orchestration, artifact preparation,
and development helpers. Dockerfiles own image dependencies; Compose owns task
services. Keep ROS setup in the shared image entrypoint and pass task-specific
commands through it. Tools and runtime consumers must not invent separate build
or package-resolution paths.

Keep `test-u22` and `test-u24` flags aligned with their Bazel config and Python
version. Test services need no GPU or real devices; do not add device access to
make a fixture pass. Existing inherited privileged mode and host `/dev` mounts
remain an isolation gap until the Compose services and contract tests remove them.
Preserve explicit config requirements on launcher tasks and the MCP separation
between preparation, connection, and operator-authorized robot launch.

## Verification

Run `bash -n` for changed shell scripts, the relevant Python/CLI tests, and
`./scripts/docker_entrypoints_test.sh` for Compose/entrypoint changes. Render
Compose configuration before executing a changed task. Tests must use temporary
artifacts, test children, fake transports, or PTYs and must not flash firmware.

## Scripts

Joshua's public entrypoints are Docker Compose services from the repo root. The Makefile provides optional shorthand.

## `joshua_mcp.sh`

Use `./scripts/joshua_mcp.sh setup` for guided prerequisite checks, preparation,
AI IDE registration and first inspection. Existing installations and policies
are preserved; other clients receive standard stdio settings. See the
[setup guide](../docs/MCP_SETUP.md) for recovery and hardware-integration steps.

Prepare the read-only MCP server with `./scripts/joshua_mcp.sh prepare`, verify
inspection with `./scripts/joshua_mcp.sh check`, and use
`./scripts/joshua_mcp.sh serve` as the client stdio command. `doctor` reports
host, Docker, artifact and source status without installing anything. Add
`--stack u24` for Jazzy; the default is Humble. `--config-root` selects the real
workspace directory exposed for inspection.

The dedicated [MCP Compose services](../docker-compose.mcp.yml) reuse the
development images without GPU reservations, privileged mode, host networking
or physical device mounts. ROS and Bazel remain in Docker. Preparation retains
complete artifacts under ignored `dist/mcp/`; serving uses the pinned image,
local Docker daemon and generation without rebuilding. Failed preparation
preserves the prior active generation. Source changes require preparation and
client reconnection before claiming that inspection reflects those changes.

If earlier Docker builds left unwritable `dist/` or `.cache/` paths,
preparation stops with `storage_unwritable` and gives commands for the exact
blocking path. Review those commands with the directory owner, repair only the
reported path, and rerun preparation. The launcher never changes ownership or
permissions automatically. The MCP cache is excluded from Docker build contexts.

## `setup.sh`

`python3 scripts/qualify_serial.py --output /tmp/joshua-serial-evidence --runs 10`
builds the mixed Teensy artifact without flashing and repeats the hardware-free
mixed/serial/coordinator tests on both supported stacks. It freezes source and
artifact hashes, PlatformIO dependencies, command logs and XML evidence in a new
directory outside the repository. Docker is the only host build dependency;
the host script uses Python's standard library. The report labels all evidence
software-only and leaves physical qualification open. See
[the qualification procedure](../docs/SERIAL_COMM_QUALIFICATION.md).

Host bootstrap for Docker tooling and PTP device access:

```bash
sudo ./scripts/setup.sh
```

It installs/checks Docker Engine, Docker Compose v2, buildx, docker group membership, and NVIDIA Container Toolkit.

It also creates a `ptp` group, adds the invoking sudo user, and installs
`/etc/udev/rules.d/99-joshua-ptp.rules` to grant group read access (`0640`) to
`/dev/ptp*`. The rule applies to existing and future clocks; no PTP device is
required during setup. Log out and back in after new group membership.

PTP setup grants device access; RobotTime reads the PHC directly without a UTC-offset setting and falls back to system UTC if unavailable. See [RobotTime](../docs/ROBOT_TIME.md#configuration-and-time-domains). The bootstrap requires an existing NVIDIA driver, but does not install the host CUDA toolkit, ROS2, Bazel, Python packages, OpenCV, CuDNN, or project runtime dependencies.

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

`pybricks_spike_bridge.py` runs on a LEGO SPIKE Prime hub and bridges line-based motor commands over stdin/stdout. Deploy it via the Pybricks app or `pybricksdev`. The host-side driver that drove it now lives in [tools/pybricks/](../tools/AGENTS.md); Joshua has no board type for a SPIKE hub, so the launcher cannot reach one. This script is kept for manual hub-side testing.

`spike_wave_publisher.py` is a ROS2 test publisher. Run it through Docker:

```bash
docker compose run --rm joshua-u22 python3 scripts/spike_wave_publisher.py --topic spike/motor_A/command --hz 20
```
