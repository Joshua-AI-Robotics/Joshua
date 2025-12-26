# Setup Scripts

This directory contains setup and build scripts for the Joshua project.
These scripts are designed to simplify the development workflow, whether you are running natively on Linux or building inside Docker containers for different architectures (x86 vs ARM64).

## 1. `setup.sh` - Environment Setup

This script sets up development and runtime environment. It handles installing system dependencies (ROS 2, Bazel, Docker, etc.) for both Ubuntu 22.04 (Humble) and 24.04 (Jazzy).

**Usage:**
```bash
# For Development (installs Bazel, Docker, pre-commit hooks, full desktop ROS)
sudo ./scripts/setup.sh --env=dev

# For Runtime (installs only base ROS libraries needed to run the robot)
sudo ./scripts/setup.sh --env=runtime
```

**What it does:**
- Detects OS version (22.04 vs 24.04) and installs the correct ROS distribution (Humble vs Jazzy).
- Installs system packages: `git`, `python3`, `docker`, `opencv`.
- Installs build tools: `bazel` (via bazelisk), `clang-format`, `buildifier`.
- Sets up Python virtual environments and dependencies.
- Configures user permissions (docker group, serial ports).

---

## 2. `build.py` - The Master Build Script

This is the main entry point for building the project for cross-platform and multi-arch. It automatically handles Docker containers to ensure clean, reproducible builds across different OS versions and CPU architectures.

**Usage:**
```bash
# Build for current OS (e.g., Ubuntu 22) on x86
./scripts/build.py //launcher:joshua_main_pkg

# Build for specific OS and Architecture
./scripts/build.py //launcher:joshua_main_pkg --os=u24 --cpu=arm64
```

**How it works:**
1. **Target Selection:** You pass a Bazel target (e.g., `//launcher:joshua_main_pkg`).
2. **Container Selection:** It picks the correct Docker image based on your flags:
   - `--os=u22` -> `joshua:u22-humble`
   - `--os=u24` -> `joshua:u24-jazzy`
3. **Architecture Handling:**
   - **x86 on x86:** Runs natively in the container.
   - **ARM64 on x86:** Uses QEMU emulation (via Docker) to build ARM64 binaries on your PC.
4. **Artifact Management:** Copies the built binaries out of the container to `dist/u{OS}/{CPU}/`.

**Key Flags:**
- `--os`: `u22` (default) or `u24`.
- `--cpu`: `x86` (default) or `arm64`.

**Note on ARM64 Builds:**
Building for ARM64 on an x86 machine uses **emulation**, which is slower than native compilation. The script automatically applies throttling flags (`--jobs=4`) to prevent memory exhaustion during the linking phase.

---

## 3. `container_build.sh` - Internal Build Logic

*You typically do not run this script manually.*

This script is mounted into the Docker container by `build.py`. It executes the actual `bazel build` command inside the clean environment and safely copies artifacts to the output directory.

**Features:**
- Finds the correct output files using `bazel cquery`.
- Handles ownership permissions (so you can edit files created by the container).
- Reports build status.

---

## Directory Structure

```text
scripts/
├── build.py             # Main build entry point (Host side)
├── container_build.sh   # Internal build logic (Container side)
├── setup.sh             # System dependency installer
└── README.md            # This file
```
