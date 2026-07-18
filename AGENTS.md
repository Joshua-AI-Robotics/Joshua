# AGENTS.md — instructions for coding agents

Joshua is a config-driven robotics framework: one protobuf config (e.g.
`config/config_preset/so100/teleoperate.pbtxt`) drives a ROS 2 stack built with
Bazel. Human-facing docs live in [README.md](README.md) and
[docs/](docs/README.md); this file is the entry point for coding agents. Launch
agents from the repository root so this file is picked up.

## Development environment

Two supported paths — [CONTRIBUTING.md](CONTRIBUTING.md) is authoritative.
Docker is recommended:

```bash
docker compose build joshua-u22       # Ubuntu 22.04 / ROS 2 Humble
docker compose run --rm joshua-u22    # interactive shell
```

Native Ubuntu 22.04 is also supported:

```bash
sudo ./scripts/setup.sh --env=dev
```

Ubuntu 24.04 / ROS 2 Jazzy is experimental and sits behind a Compose profile
(`docker compose --profile u24 run --rm joshua-u24`). Development is supported
on **Ubuntu Linux** only; macOS is not. ARM64, Jazzy, and Isaac Sim variants
are documented in [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md).

## Test

Run these **inside a dev shell**, or natively after `setup.sh --env=dev` — a
bare host without ROS 2 and Bazel will fail:

```bash
bazel test //...   # what CI runs on every PR
```

For targeted work, run the package you touched:

```bash
bazel test //ros2/utils:packet_parser_test
```

To run one-shot from the host without opening a shell:

```bash
docker compose run --rm joshua-u22 bazel test //...
```

CI tests and normal package builds do not require GPU hardware.

## Lint

Before pushing:

```bash
./hooks/lint_check.sh --fix   # auto-fix files changed since diverging from develop
```

Covers `clang-format` (C/C++/proto) and `black`/`isort`/`flake8` (Python). After
`setup.sh --env=dev`, pre-commit runs these automatically on **pre-push**.

`BUILD`/`BUILD.bazel` files are **not** covered by that script — run
`buildifier` on them yourself when you edit them.

## Conventions

- All PRs target the **`develop`** branch.
- Branch naming: `<github-username>/<topic>` (e.g. `djkim/initialize-web-app`).
- Record user-facing changes in [CHANGELOG.md](CHANGELOG.md).
- The protobuf config is the single source of truth for a robot setup; don't
  duplicate configuration into code.

## Read before you edit

Links are not loaded automatically — actually open these files when the work
touches their area:

- **Any subsystem** (`ai/`, `ros2/`, `simulation/`, `ui/`, `firmware/`,
  `scripts/`, `node_generator/`, `robot/comm/ethercat/`): read that
  directory's `README.md` first.
- **Build/packaging or release artifacts**: read
  [scripts/README.md](scripts/README.md) and the "Testing and CI" section of
  [CONTRIBUTING.md](CONTRIBUTING.md).
- **Cross-cutting or architectural changes**: read
  [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
- **Environment/setup variants** (ARM64, Isaac Sim, hardware runs): read
  [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md).
