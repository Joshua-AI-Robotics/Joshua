# Contributing to Joshua

Thank you for your interest in [Joshua](https://github.com/Joshua-AI-Robotics/Joshua) — a config-driven robotics stack on ROS 2 and Bazel. This guide reflects how the project is developed today.

- **Project website:** [joshua-ai-robotics.org](https://joshua-ai-robotics.org/)
- **Default branch:** `develop` (all PRs target this branch)
- **License:** [Apache License 2.0](LICENSE) — by contributing, you agree your work is licensed under the same terms.

## Ways to contribute

You do not need to write code to help:

- **Report bugs** — [open an issue](https://github.com/Joshua-AI-Robotics/Joshua/issues/new/choose) with steps to reproduce, environment details, and logs.
- **Report security issues** — follow [SECURITY.md](SECURITY.md); do not use public issues.
- **Suggest features** — describe the problem, your proposed approach, and which robot or preset it affects.
- **Improve docs** — README, `docs/`, preset comments, and module READMEs are always welcome.
- **Submit code** — follow the workflow below.

For open-ended questions or ideas, prefer [GitHub Discussions](https://github.com/Joshua-AI-Robotics/Joshua/discussions) when available.

## Development setup

Choose one path:

### Docker (recommended for first-time contributors)

```bash
git clone https://github.com/Joshua-AI-Robotics/Joshua.git
cd Joshua
docker compose build joshua-u22
docker compose run joshua-u22
```

Inside the container:

```bash
bazel run launcher:joshua_main
```

See [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md) for ARM64 and ROS Jazzy Docker images. Development is supported on **Ubuntu Linux** only; macOS is not supported.

### Native Ubuntu 22.04

```bash
git clone https://github.com/Joshua-AI-Robotics/Joshua.git
cd Joshua
sudo ./scripts/setup.sh --env=dev
```

`setup.sh --env=dev` installs ROS 2, Bazel (via Bazelisk), Docker, `clang-format`, `buildifier`, Python tooling, and **pre-commit hooks**.

More detail: [scripts/README.md](scripts/README.md).

If you develop with an AI coding agent (Claude Code, Copilot, Codex, …), the canonical agent instructions live in [AGENTS.md](AGENTS.md); launch your agent from the repository root so it picks the file up.

## Branch and PR workflow

This project uses **pull requests into `develop`**. Recent history follows this pattern:

1. Fork the repo (or branch from `develop` if you are a maintainer).
2. Create a feature branch.
3. Make focused commits.
4. Rebase or merge `develop` before opening the PR if the branch is long-lived.
5. Open a PR against **`develop`**.
6. Fill in the [PR template](.github/pull_request_template.md).
7. Wait for **CI** and a maintainer review.

### Branch naming

Use a short, descriptive name. The most common convention in this repo is:

```text
<github-username>/<topic>
```

Examples from project history:

- `hmoon/refactor_actuator_subscriber`
- `kyoon/pybricks_on_python`
- `djkim/initialize-web-app`
- `ulee/smolvla_int_2`
- `theo/ubuntu_docker`

Optional prefixes also appear for scoped work:

- `docs/<topic>` — documentation only
- `fix/<topic>` — bug fixes
- `feature/<topic>` — new functionality

Pick one style and keep the branch focused on a single change where possible.

### Commit messages

There is no strict format enforced today. Clear, descriptive messages work best:

- `Fix ActionPacket payload handling in ROS2 nodes`
- `docs: slim README and add docs/ guides`
- `Organize config presets into subdirectories by robot/category`

Prefer present tense or imperative mood. If a commit fixes an issue, mention it in the PR body (`Fixes #123`).

### Pull request checklist

Before requesting review:

- [ ] PR targets **`develop`**
- [ ] Summary explains **what** changed and **why**
- [ ] You ran tests locally (see below)
- [ ] Docs or presets updated if behavior or config changed
- [ ] Pre-push hooks pass (if installed)
- [ ] For proto / packet changes: relevant unit tests updated and passing

## Testing and CI

GitHub Actions runs on every PR to `develop`:

```bash
bazel test //...
```

Run the same command locally before pushing. For targeted work, run the package you touched, for example:

```bash
bazel test //ros2/utils:packet_parser_test
bazel test //<package>:<target>
```

CI uses **Ubuntu 22.04** and **ROS 2 Humble**. If your change depends on Jazzy or ARM64, say so in the PR and describe how you tested.

To exercise the full stack manually:

```bash
bazel run launcher:joshua_main
bazel run //launcher:joshua_main -- --config config/config_preset/so100/teleoperate.pbtxt
```

Preset-specific flows: [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md).

## Code style and linting

After `setup.sh --env=dev`, pre-commit runs on **pre-push**:

| Language / file | Tool |
|-----------------|------|
| C, C++, `.proto` | `clang-format` (see [.clang-format](.clang-format)) |
| Python | `black`, `isort`, `flake8` |
| YAML | syntax validation |

Manual check or auto-fix files changed since your branch diverged from `develop`:

```bash
./hooks/lint_check.sh              # check changed files (quiet by default)
./hooks/lint_check.sh --fix        # auto-fix changed files
./hooks/lint_check.sh --verbose    # show summary and per-file fix output
```

Bazel `BUILD` / `BUILD.bazel` files should be kept tidy with `buildifier` when you edit them.

Keep changes consistent with surrounding code — match naming, structure, and documentation level in the file you are editing.

## Where to make changes

Joshua is organized around a single protobuf config that drives the runtime graph. Common contribution areas:

| Area | Path | Notes |
|------|------|-------|
| Config & presets | `config/`, `config/config_preset/` | Add or adjust `.pbtxt` presets; keep robot-specific files grouped |
| Protobuf schemas | `config/proto/`, `ros2/proto/` | Coordinate with node generator and packet parser |
| ROS 2 nodes | `ros2/` | Actions, perceptions, bridges |
| Node generator | `node_generator/` | Turns config into node definitions |
| Launcher | `launcher/` | `joshua_main` entry point |
| AI & data | `ai/` | Policies and dataset collection (DataStore) |
| Simulation | `simulation/` | Models and sim presets |
| Web UI | `ui/` | React control panel |
| Docker & builds | `dockerfiles/`, `scripts/` | Cross-platform and ARM64 builds |

Start with [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for how config, protos, and ROS 2 connect.

### High-impact changes (extra care)

These areas affect many presets and downstream nodes:

- **`action_packet.proto`** and [`ros2/utils/packet_parser`](ros2/utils/packet_parser.md) — update registries and run `bazel test //ros2/utils:packet_parser_test`
- **Top-level protos** (`config.proto`, `robot.proto`, `ai.proto`) — may require node generator and preset updates
- **Breaking preset moves** — update docs and any referenced paths in README / GETTING_STARTED

## Releases and versioning

- **Single source of truth:** [`VERSION`](VERSION)
- **Changelog:** [`CHANGELOG.md`](CHANGELOG.md) — add entries under `[Unreleased]` with each user-facing change
- **SemVer:** `0.x.y` while the API is pre-1.0; breaking changes may land in minor releases
- **Tags:** `vX.Y.Z` on `develop` when cutting a release (for example `v0.1.0`)
- Keep [`MODULE.bazel`](MODULE.bazel) `version` and [`ui/package.json`](ui/package.json) in sync with `VERSION`

## Review process

- Every PR into `develop` needs **at least one approving review** before it can
  be merged.
- A maintainer will review PRs for correctness, test coverage, and fit with the config-driven architecture.
- Reviewers are routed by [`.github/CODEOWNERS`](.github/CODEOWNERS). Touching
  an owned path auto-requests that owner, and their approval is the one that
  satisfies the requirement for the PR.

  **Every top-level directory has an owner**, enforced per-PR by the
  `CODEOWNERS coverage` CI job (`hooks/codeowners_check.sh`) — adding a new
  top-level directory fails the build until it is given one.

  [@hsmoon5458](https://github.com/hsmoon5458) is co-owner of every path, so a
  review is never blocked on one person being away. Subsystems with a dedicated
  owner:

  | Area | Paths | Owners |
  | --- | --- | --- |
  | Board layer | `robot/board/`, `docs/BOARD_LAYER_RFC.md` | [@heostar](https://github.com/heostar), [@piscesgh](https://github.com/piscesgh), [@hsmoon5458](https://github.com/hsmoon5458) |
  | Communication layer | `robot/comm/` | [@piscesgh](https://github.com/piscesgh), [@hsmoon5458](https://github.com/hsmoon5458) |
  | Firmware | `firmware/` | [@heostar](https://github.com/heostar), [@hsmoon5458](https://github.com/hsmoon5458) |
  | Web UI | `ui/` | [@donegjookim](https://github.com/donegjookim), [@hsmoon5458](https://github.com/hsmoon5458) |
  | Docker images | `dockerfiles/` | [@donegjookim](https://github.com/donegjookim), [@hsmoon5458](https://github.com/hsmoon5458) |
  | Docs | `docs/` (except `docs/BOARD_LAYER_RFC.md`, owned by the board layer) | [@heostar](https://github.com/heostar), [@piscesgh](https://github.com/piscesgh), [@donegjookim](https://github.com/donegjookim), [@hsmoon5458](https://github.com/hsmoon5458) — everyone with write access |
  | Everything else | the remaining top-level directories and root files | [@hsmoon5458](https://github.com/hsmoon5458) |

  Two owners on a line means **either** may approve — not that both must. And a
  rule on a subdirectory *replaces* its parent's rule rather than adding to it,
  so each entry repeats every owner it wants.

- Address review comments with new commits on the same branch.
- PRs may be closed after extended inactivity; you can reopen or rebase when ready.

## Getting help

- **Docs index:** [docs/README.md](docs/README.md)
- **Issues:** [github.com/Joshua-AI-Robotics/Joshua/issues](https://github.com/Joshua-AI-Robotics/Joshua/issues)
- **Security:** [SECURITY.md](SECURITY.md)
- **Website:** [joshua-ai-robotics.org](https://joshua-ai-robotics.org/)

## Code of conduct

All participants are expected to follow our [Code of Conduct](CODE_OF_CONDUCT.md).
Be respectful and constructive — we are building tools for real robots and real collaborators.
