# Changelog

All notable changes to Joshua are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Version numbers are defined in [`VERSION`](VERSION). Git tags use the form `vX.Y.Z`
(for example, `v0.2.3`).

## [Unreleased]

### Added

- `robot/board/factory/board_resolver.h`: one implementation of
  `(board_name, channel)` resolution against `boards{}`, so every layer that
  binds to a board channel resolves it the same way and reports the same
  errors ([docs/BOARD_LAYER_RFC.md](docs/BOARD_LAYER_RFC.md) §6.5)
- Docker Compose task services for shells, tests, launcher runs, package builds,
  UI, and Isaac-backed simulation overlays, with optional Makefile aliases
- Isaac Sim as a plain simulation backend (`SIM_BACKEND_ISAAC_SIM`):
  new `simulation/isaac/` launcher + viewer, and the `IsaacSimConfig` proto
  ([simulation/README.md](simulation/README.md)). No Isaac preset ships — the
  `*_sim_isaac.pbtxt` presets were removed again later in this release
- `simulation/README.md` covering the MuJoCo modes and the Isaac Sim backend
- [AGENTS.md](AGENTS.md) canonical instructions for AI coding agents
  (open [agents.md](https://agents.md/) standard), with pointer bridges for
  tools that do not read it natively: Claude Code (`CLAUDE.md`), Gemini CLI
  (`GEMINI.md`), and Copilot IDE surfaces
  (`.github/copilot-instructions.md`)
- `robot/README.md`, `config/README.md`, and `tools/README.md`, covering the
  three subsystems that previously had no documentation
- `hooks/agents_doc_check.sh` and an `agent-docs` CI job, failing the build when
  a Markdown link or `@`-import in the agent instruction files stops resolving,
  or when a nested `AGENTS.md` lacks a `CLAUDE.md` bridge that actually imports
  it
- `.gemini/settings.json` setting `context.fileName`, so Gemini CLI reads
  `AGENTS.md` natively at every level instead of only the root `GEMINI.md`

### Fixed

- Docker images now source the ROS 2 environment through a shared entrypoint
  ([docker/entrypoint.sh](docker/entrypoint.sh)), so non-interactive task
  commands (e.g. `docker compose run --rm run-u22`) no longer fail with an
  empty `AMENT_PREFIX_PATH` / `librcl_action.so` import error
- Launcher default `--config` and all docs now point to the renamed preset
  `config/config_preset/so100/teleoperate.pbtxt` (old `so100_teleoperate.pbtxt`
  references were broken since the 0.2.2 rename)
- `docs/ARCHITECTURE.md` diagram updated to `joshua_summary.png` (old image was removed)
- Docs preset table and `simulation/main.py` docstring corrected to the actual
  `so100/sim_interactive.pbtxt` / `sim_mirror.pbtxt` filenames
- Docker docs now include the required Compose `--profile` flags for ARM64, Jazzy,
  and the web UI (`production` profile) services
- `scripts/setup.sh` now removes conflicting Ubuntu-archive Docker packages
  (`docker.io`, `docker-compose`, `docker-compose-v2`, etc.) before installing
  the docker.com packages, so hosts with stock Ubuntu Docker already installed
  no longer hit a `dpkg` "trying to overwrite" error on `docker-compose-plugin`

### Removed

- **All Python from the robot layer** (BOARD_LAYER_RFC.md §10 Phase 9):
  `action_factory.py`, `perception_factory.py`, `comm_factory.py`, the action
  and perception interface base classes, `serial.py`, and every mock driver.
  `robot/` is now C++ only
- Python hardware ROS 2 nodes `actuator_subscriber.py`, `camera_publisher.py`,
  `encoder_publisher.py`, and `lidar_publisher.py` with their
  `ros2_py_binary` targets. The identically named C++ binaries are now the
  only implementations; `node_generator` no longer selects between a C++ and
  a Python backend, and the C++→Python launch fallback is gone.
  `trajectory_publisher`, `data_subscriber`, and `inference` stay Python —
  they have no hardware access
- Mock components: `MOTOR_MOCK`/`MOCK_MOTOR`/`MockMotorConfig` and
  `MOCK_CAMERA`/`MOCK_ENCODER`/`MOCK_LIDAR`, the
  C++ `MockMotorDriver`, and `example/mock_py_test.pbtxt`. `BoardType::MOCK`
  is unaffected — it is C++ test infrastructure, not a mock driver
- The operational-limit calibration subsystem:
  `ros2/operational_limit_calibration.cc`, `config/proto/calibration.proto`
  (`Calibration`, `SingleCalibration`, `CalibrationMode`), the
  `Config.calibration` field, `MODE_CALIBRATION`, and the
  `OPERATIONAL_LIMIT_CALIBRATION` node type.
  Set `operational_lower_limit` / `operational_upper_limit` by hand in the
  preset instead
- The ant, trileg, and bileg simulation presets
  (`ant_sim_interactive.pbtxt`, `ant_sim_isaac.pbtxt`, `trileg_sim_isaac.pbtxt`,
  `bileg_sim_isaac.pbtxt`) and `so100/calibrate_leader_arm_operational_limit.pbtxt`.
  No Isaac Sim preset ships any more; the Isaac backend and the
  ant/trileg/bileg model assets are still present, so a preset can be written
  against them
- `docs/TRAINING_RFC.md`, which described the RL training pipeline removed
  earlier in this release
- The `python_spike_*` presets and `docs/pybricks_test.md` /
  `docs/spike_python_driver_plan.md`, which documented driving a SPIKE hub
  from a preset — no longer possible
- Host-side `scripts/build.py`; package builds now run through Compose services
- RL training pipeline (`ai/train` trainer, Isaac Lab task/env builders,
  trajectory export, `training.proto`, `training_launcher.cc`, the
  `MODE_TRAINING` operation mode, and all 19 train/eval/export presets).
  Isaac Sim remains available as a simulation backend
- Unused RL-era pip dependencies (`jax[cuda12]`, `flax`, `optax`,
  `stable-baselines3`, `mujoco-mjx`, direct `gymnasium`) -- nothing in the
  codebase imported them; `requirements.lock` shrinks by ~140 lines
- Orphaned `isaac_sim/` directory (typo-named, unreferenced legacy USD asset)
- Empty `.gitmodules` and an accidentally committed `__pycache__` bytecode file
- Debug `console.log` noise from the web UI hooks and Vite proxy config

### Changed

- **Breaking (wire format): proto field and enum numbers were compacted.**
  The numbers freed by the removals above were reused rather than reserved,
  so this release is not wire-compatible with data serialized by an earlier
  one. Affects `MotorType`, `ActuatorType`, `Actuator` fields (`channel` and
  the `action_config` oneof), `BoardType`, `CommType`, `NodeType`,
  `OperationMode`, and `Config.simulation`. Note `BoardType::ESP32`, which
  landed on `develop` as `8`, is `7` here — the compaction freed the number.
  Text-format `.pbtxt` presets are unaffected — they bind by name.
  Re-generate any stored binary protos
- **LEGO SPIKE / Pybricks hubs are no longer supported by the launcher.** The
  Python driver, the `SPIKE_HUB_BLE` stub board, `BoardType::SPIKE_HUB_BLE`,
  and `MotorType::MOTOR_SPIKE` were all removed; no preset can reach a hub.
  A motor can still be driven by hand with
  `bazel run //tools/pybricks:pybricks_ble_smoke`, and the hub-side
  `scripts/pybricks_spike_bridge.py` (MicroPython, runs on the brick) is
  unaffected. Nothing Spike-shaped remains outside `tools/pybricks/`:
  `ActuatorType::SPIKE_MOTOR`, `SpikeMotorConfig`, and `CommType::BLE` (whose
  only user was the Spike hub) are removed from the robot protos, and the
  bench tool configures itself with its own
  `SpikeMotorSpec` dataclass. See
  [BOARD_LAYER_RFC.md](docs/BOARD_LAYER_RFC.md) §10 Phase 9
- The Pybricks bench tooling moved out of `robot/` to `tools/pybricks/`
  (`//robot/action/motors/drivers:pybricks_driver_py` →
  `//tools/pybricks:pybricks_driver`). The `//tools/pybricks:pybricks_ble_smoke`
  label is unchanged. `PybricksMotorDriver` no longer implements
  `ActuatorInterface`, which was deleted with the Python robot layer
- No preset can exercise the node graph without real hardware any more, since
  the mock components are gone. `bazel test` and config validation remain the
  hardware-free verification path
- Native Ubuntu setup is no longer a supported development entrypoint;
  `scripts/setup.sh` now bootstraps/checks Docker host tooling only
- CI now runs Bazel tests through Docker for both Ubuntu 22/Humble and
  Ubuntu 24/Jazzy
- AM243 now supports both config-driven transports: the existing TI EtherCAT
  demo and the shared `joshua_wire_v1` serial frame transport used by Teensy.
- Added a buildable LP-AM243 firmware overlay that runs the TI EtherCAT demo
  and a motion-safe `joshua_wire_v1` UART task in the same image.
- Added an AM243 serial demo smoke that prints firmware identity, channel
  capabilities, command results, feedback, and round-trip timing.
- `simulation/` restructured: backend code now lives in symmetric
  `simulation/mujoco/` (engine + modes) and `simulation/isaac/`
  (launcher + viewer) packages, and `simulation/models/` is organized
  one directory per robot (`so_arm100/`, `ant/`, `trileg/`, `bileg/`);
  preset `model_path`/`usd_filename` values updated accordingly
- `.gitignore` covers `.venv/`, `node_modules/`, and common Python tooling caches
- `scripts/README.md` documents all helper scripts (mock serial ports, Docker
  entrypoint, SPIKE bridge and wave publisher)
- Code of Conduct now lists a concrete private reporting channel

## [0.2.3] - 2026-06-08

### Added

- [`VERSION`](VERSION) as single source of truth for releases
- [`CHANGELOG.md`](CHANGELOG.md) with project history
- [`CONTRIBUTING.md`](CONTRIBUTING.md), [`SECURITY.md`](SECURITY.md), and [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md)
- README section for deployable binaries via `scripts/build.py`
- Releases and versioning guidance in contributing docs

### Changed

- README diagram updated to `joshua_summary.png`; legacy diagram assets removed
- Documentation states **Ubuntu Linux only** (macOS not supported)
- Pre-push lint hook checks only files changed since `develop` (quiet by default)
- Apache 2.0 license, [`NOTICE`](NOTICE), and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) updated ([#50](https://github.com/Joshua-AI-Robotics/Joshua/pull/50))

### Removed

- Qt-based UI; project standardizes on the React web control panel ([#51](https://github.com/Joshua-AI-Robotics/Joshua/pull/51))

## [0.2.2] - 2026-06-04

### Added

- Trajectory runner and constant trajectory export from trained models ([#46](https://github.com/Joshua-AI-Robotics/Joshua/pull/46), [#47](https://github.com/Joshua-AI-Robotics/Joshua/pull/47))
- Unified training launcher in `joshua_main` with config-driven dispatch ([#43](https://github.com/Joshua-AI-Robotics/Joshua/pull/43))
- Isaac Lab simulation pipeline refactor and training presets ([#41](https://github.com/Joshua-AI-Robotics/Joshua/pull/41))
- MuJoCo integration, interactive viewer, and SO100 sim examples ([#40](https://github.com/Joshua-AI-Robotics/Joshua/pull/40))
- MJX (JAX) training path for ant and related sim models
- Bileg robot training presets

### Changed

- Actuator subscriber refactor: `ActionPacket` payload handling, topic wiring, and packet parser updates ([#49](https://github.com/Joshua-AI-Robotics/Joshua/pull/49))
- SO100 config presets reorganized and renamed under `config/config_preset/so100/`
- Pybricks BLE driver improvements ([#48](https://github.com/Joshua-AI-Robotics/Joshua/pull/48))
- Config presets grouped into subdirectories by robot/category

### Fixed

- SO-100 trajectory and normalized `complex.position` handling in `ActionPacket`

## [0.2.1] - 2026-02-18

### Added

- Python node generator support and Python ROS 2 nodes ([#37](https://github.com/Joshua-AI-Robotics/Joshua/pull/37))
- Pybricks motor support on Python stack ([#38](https://github.com/Joshua-AI-Robotics/Joshua/pull/38))
- Zenoh bridge for ROS 2 DDS ([#33](https://github.com/Joshua-AI-Robotics/Joshua/pull/33))
- SmolVLA inference integration and refactor ([#30](https://github.com/Joshua-AI-Robotics/Joshua/pull/30), [#36](https://github.com/Joshua-AI-Robotics/Joshua/pull/36))
- OpenCV camera publisher improvements ([#35](https://github.com/Joshua-AI-Robotics/Joshua/pull/35))
- Docker images for Ubuntu 22.04 and 24.04 ([#31](https://github.com/Joshua-AI-Robotics/Joshua/pull/31))
- Episode index and dataset recording controls in web UI ([#32](https://github.com/Joshua-AI-Robotics/Joshua/pull/32))

### Changed

- SmolVLA model loading and inference pipeline refactored for maintainability

## [0.2.0] - 2025-12-24

### Added

- React web control panel (`ui/`) ([#29](https://github.com/Joshua-AI-Robotics/Joshua/pull/29))
- Cross-platform build script `scripts/build.py` and `joshua_main_pkg` deployable tarball ([#34](https://github.com/Joshua-AI-Robotics/Joshua/pull/34))
- Data collecting node for dataset capture ([#19](https://github.com/Joshua-AI-Robotics/Joshua/pull/19))
- Inference base class and refactored inference nodes ([#12](https://github.com/Joshua-AI-Robotics/Joshua/pull/12), [#18](https://github.com/Joshua-AI-Robotics/Joshua/pull/18), [#27](https://github.com/Joshua-AI-Robotics/Joshua/pull/27))
- Node proto refactor and updated node generator ([#25](https://github.com/Joshua-AI-Robotics/Joshua/pull/25), [#28](https://github.com/Joshua-AI-Robotics/Joshua/pull/28))
- ROS 2 rules and QoS profile setup ([#17](https://github.com/Joshua-AI-Robotics/Joshua/pull/17), [#24](https://github.com/Joshua-AI-Robotics/Joshua/pull/24))
- LDS-01 lidar publisher ([#13](https://github.com/Joshua-AI-Robotics/Joshua/pull/13))
- Camera CV pipeline ([#26](https://github.com/Joshua-AI-Robotics/Joshua/pull/26))
- ARM64 cross-compilation and install script fixes ([#21](https://github.com/Joshua-AI-Robotics/Joshua/pull/21), [#22](https://github.com/Joshua-AI-Robotics/Joshua/pull/22))

### Changed

- Repository cleanup and build/install path refactor ([#23](https://github.com/Joshua-AI-Robotics/Joshua/pull/23))
- Logging migrated to `glog`; log level configuration improved
- Mock perception pipeline (camera, encoder, lidar) for development without hardware

### Fixed

- Bazel install script and factory registration on ROS 2 nodes ([#14](https://github.com/Joshua-AI-Robotics/Joshua/pull/14), [#21](https://github.com/Joshua-AI-Robotics/Joshua/pull/21))

## [0.1.0] - 2025-10-13

### Added

- Config-driven robotics stack: single protobuf `.pbtxt` launches ROS 2 nodes via `joshua_main`
- Node generator for action, perception, and AI nodes
- STS3215 servo driver and serial actuator support
- Abstract action/perception factories ([#11](https://github.com/Joshua-AI-Robotics/Joshua/pull/11))
- Apache 2.0 license and attribution files ([#9](https://github.com/Joshua-AI-Robotics/Joshua/pull/9))
- Docker-based development environment and setup scripts
- CI workflow (`bazel test //...` on Ubuntu 22.04 + ROS 2 Humble)
- Camera troubleshooting documentation ([#3](https://github.com/Joshua-AI-Robotics/Joshua/pull/3))

### Changed

- Codebase cleanup and node generator build flow ([#6](https://github.com/Joshua-AI-Robotics/Joshua/pull/6), [#7](https://github.com/Joshua-AI-Robotics/Joshua/pull/7))
- ARM64 cross-compile support ([#8](https://github.com/Joshua-AI-Robotics/Joshua/pull/8))

### Fixed

- Qt6 build setup for early control panel ([#1](https://github.com/Joshua-AI-Robotics/Joshua/pull/1))

[Unreleased]: https://github.com/Joshua-AI-Robotics/Joshua/compare/v0.2.3...develop
[0.2.3]: https://github.com/Joshua-AI-Robotics/Joshua/compare/v0.2.2...v0.2.3
[0.2.2]: https://github.com/Joshua-AI-Robotics/Joshua/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/Joshua-AI-Robotics/Joshua/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/Joshua-AI-Robotics/Joshua/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/Joshua-AI-Robotics/Joshua/releases/tag/v0.1.0
