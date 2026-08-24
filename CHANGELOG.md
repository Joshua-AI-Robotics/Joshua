# Changelog

All notable changes to Joshua are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Version numbers are defined in [`VERSION`](VERSION). Git tags use the form `vX.Y.Z`
(for example, `v0.2.3`).

## [Unreleased]

### Added

- Arduino Uno R3 on the board layer: `ArduinoBoard`,
  `firmware/arduino/uno/` (`joshua_wire_v1` over UART serial), and
  `config/config_preset/example/arduino_stepper_demo.pbtxt`. First
  hardware goal is IDENTIFY-only (USB, no motor); flash and motion are
  still open
  ([`firmware/arduino/uno/README.md`](firmware/arduino/uno/README.md))
- Isaac Sim as a plain simulation backend (`SIM_BACKEND_ISAAC_SIM`):
  new `simulation/isaac/` launcher + viewer, `IsaacSimConfig` proto, and
  `ant`/`trileg`/`bileg` `*_sim_isaac.pbtxt` presets ([simulation/README.md](simulation/README.md))
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

- Launcher default `--config` and all docs now point to the renamed preset
  `config/config_preset/so100/teleoperate.pbtxt` (old `so100_teleoperate.pbtxt`
  references were broken since the 0.2.2 rename)
- `docs/ARCHITECTURE.md` diagram updated to `joshua_summary.png` (old image was removed)
- Docs preset table and `simulation/main.py` docstring corrected to the actual
  `so100/sim_interactive.pbtxt` / `sim_mirror.pbtxt` filenames
- Docker docs now include the required Compose `--profile` flags for ARM64, Jazzy,
  and the web UI (`production` profile) services

### Removed

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

- `simulation/` restructured: backend code now lives in symmetric
  `simulation/mujoco/` (engine + modes) and `simulation/isaac/`
  (launcher + viewer) packages, and `simulation/models/` is organized
  one directory per robot (`so_arm100/`, `ant/`, `trileg/`, `bileg/`);
  preset `model_path`/`usd_filename` values updated accordingly
- `.gitignore` covers `.venv/`, `node_modules/`, and common Python tooling caches
- `scripts/README.md` documents all helper scripts (mock serial ports, Docker
  entrypoint, SPIKE bridge and wave publisher); docs index links Pybricks guides
- Code of Conduct now lists a concrete private reporting channel

## [0.2.3] - 2026-06-08

### Added

- [`VERSION`](VERSION) as single source of truth for releases
- [`CHANGELOG.md`](CHANGELOG.md) with project history
- [`CONTRIBUTING.md`](CONTRIBUTING.md), [`SECURITY.md`](SECURITY.md), and [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md)
- README section for deployable binaries via [`scripts/build.py`](scripts/build.py)
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
- Cross-platform build script [`scripts/build.py`](scripts/build.py) and `joshua_main_pkg` deployable tarball ([#34](https://github.com/Joshua-AI-Robotics/Joshua/pull/34))
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
