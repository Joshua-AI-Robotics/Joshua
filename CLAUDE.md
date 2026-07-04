# CLAUDE.md

Joshua is a config-driven robotics stack: one protobuf text config (`.pbtxt`) defines
hardware, AI policy, and operation mode; the launcher builds and runs the matching
ROS 2 nodes. Bazel monorepo, Ubuntu Linux only (CI: Ubuntu 22.04 / ROS 2 Humble).

## Build & test

```bash
bazel test //...                          # full suite (same as CI)
bazel test //<package>:<target>           # targeted, e.g. //ros2/utils:packet_parser_test
bazel run launcher:joshua_main            # run the full stack
bazel run //launcher:joshua_main -- --config config/config_preset/so100/teleoperate.pbtxt
```

Packaged/deployable builds go through `./scripts/build.py` (runs Bazel in Docker) — see `scripts/README.md`.

## Lint & format

```bash
./hooks/lint_check.sh --fix    # auto-fix files changed since develop (run before committing)
```

- C/C++/`.proto`: `clang-format` (`.clang-format` at repo root)
- Python: `black`, `isort`, `flake8`
- `BUILD` / `BUILD.bazel` / `.bzl`: `buildifier`

## Repo map

- `config/` — protobuf schemas and `.pbtxt` presets (`config/config_preset/<robot>/`); the source of truth for a robot setup
- `ros2/` — ROS 2 nodes: actions, perceptions, bridges, packet parsing
- `node_generator/` — turns config into node definitions
- `launcher/` — `joshua_main` entry point
- `ai/` — policies and dataset collection (DataStore)
- `simulation/` — MuJoCo / Isaac Sim models and presets
- `ui/` — React web control panel
- `docs/ARCHITECTURE.md` — how config, protos, and ROS 2 connect; read first for cross-cutting changes

Ignore the `bazel-*` symlink directories when searching.

## Conventions

- All PRs target **`develop`** (not main); branches are `<github-username>/<topic>` (e.g. `hmoon/integrate_mujoco`)
- Commit messages: imperative mood, no strict format
- Proto / packet changes are high-impact: update the relevant unit tests and coordinate with `node_generator/` and the packet parser
- Match the naming, structure, and comment density of the surrounding code
