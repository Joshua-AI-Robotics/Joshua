# GPU-First Docker Development Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make NVIDIA GPU access the default for Joshua development shells and launcher runs while keeping tests, builds, UI, and CI GPU-independent.

**Architecture:** `docker-compose.yml` owns default NVIDIA reservations for interactive and launcher services. CPU-only services clear inherited reservations, and `docker-compose.cpu.yml` provides an explicit developer fallback. The host bootstrap installs NVIDIA Container Toolkit by default and supports `--cpu` to skip it.

**Tech Stack:** Docker Compose v2, NVIDIA Container Toolkit, Bash, Bazel, PyTorch

---

### Task 1: Replace the optional-GPU contract with a GPU-first contract

**Files:**
- Modify: `scripts/docker_entrypoints_test.sh`

- [ ] **Step 1: Require default GPU reservations**

Check the merged base configuration for NVIDIA GPU reservations on `joshua-u22`, `joshua-u24`, `run-u22`, and `run-u24`.

- [ ] **Step 2: Require CPU-only tests and builds**

Check `test-u22`, `test-u24`, `build-u22-x86`, and `build-u24-x86` and fail if any retains an NVIDIA device reservation.

- [ ] **Step 3: Require the CPU override and setup semantics**

Validate `docker-compose.cpu.yml`, require the four developer services to have no GPU reservation after the override, require `scripts/setup.sh --cpu`, and reject the obsolete `docker-compose.gpu.yml` file.

- [ ] **Step 4: Verify the contract fails before implementation**

Run: `./scripts/docker_entrypoints_test.sh`

Expected: FAIL because default services do not yet reserve GPUs or because the CPU override is absent.

### Task 2: Make selected Compose services GPU-first

**Files:**
- Modify: `docker-compose.yml`
- Create: `docker-compose.cpu.yml`
- Delete: `docker-compose.gpu.yml`

- [ ] **Step 1: Add the reusable NVIDIA reservation**

Define a YAML fragment using Docker's device reservation shape: `driver: nvidia`, `count: all`, and `capabilities: [gpu]`.

- [ ] **Step 2: Apply GPU defaults to developer services**

Apply the fragment to `joshua-u22` and `joshua-u24`. Confirm `run-u22` and `run-u24` inherit the reservation through `extends`.

- [ ] **Step 3: Clear inherited GPUs from CPU-only services**

Use Compose's documented `!reset []` merge tag on device reservations for `test-u22`, `test-u24`, `build-u22-x86`, and `build-u24-x86`.

- [ ] **Step 4: Add the CPU fallback overlay**

In `docker-compose.cpu.yml`, clear device reservations for `joshua-u22`, `joshua-u24`, `run-u22`, and `run-u24` with `!reset []`.

- [ ] **Step 5: Run the contract**

Run: `./scripts/docker_entrypoints_test.sh`

Expected: Compose checks advance to the setup or documentation requirement.

### Task 3: Make NVIDIA host setup the default

**Files:**
- Modify: `scripts/setup.sh`

- [ ] **Step 1: Invert argument handling**

Default `INSTALL_GPU=true`, accept exactly `--cpu` to set it false, and reject `--gpu` and all other arguments with `Usage: sudo ./scripts/setup.sh [--cpu]`.

- [ ] **Step 2: Preserve driver validation and toolkit installation**

For the default path, require `nvidia-smi`, install NVIDIA Container Toolkit, configure Docker with `nvidia-ctk`, restart Docker, and verify the NVIDIA runtime. Do not install the host CUDA toolkit or GPU driver.

- [ ] **Step 3: Print mode-specific next commands**

Default setup prints `docker compose run --rm joshua-u22 nvidia-smi`. CPU setup prints the command using `docker-compose.cpu.yml`.

- [ ] **Step 4: Run Bash and argument checks**

Run: `bash -n scripts/setup.sh` and invoke the script without sudo using an invalid flag to verify it rejects the flag before privileged operations.

### Task 4: Rewrite documentation for GPU-first usage

**Files:**
- Modify: `README.md`
- Modify: `docs/GETTING_STARTED.md`
- Modify: `scripts/README.md`
- Modify: `docs/superpowers/specs/2026-07-04-compose-gpu-support-design.md`

- [ ] **Step 1: Lead with default GPU commands**

Document `sudo ./scripts/setup.sh`, `docker compose run --rm joshua-u22 nvidia-smi`, and the normal `run-u22` launcher command without an overlay.

- [ ] **Step 2: Document explicit CPU fallback**

Document `sudo ./scripts/setup.sh --cpu` and commands composed from `docker-compose.yml` plus `docker-compose.cpu.yml`.

- [ ] **Step 3: Explain dependency ownership**

State that the host supplies the NVIDIA driver and Container Toolkit while CUDA-enabled Python packages remain inside model environments.

- [ ] **Step 4: Run the entrypoint contract until green**

Run: `./scripts/docker_entrypoints_test.sh`

Expected: `Docker entrypoint contract passed.`

- [ ] **Step 5: Commit the revised feature**

Commit the Compose files, setup script, test contract, and documentation as one GPU-first behavior change.

### Task 5: Install and verify the local NVIDIA runtime

**Files:**
- Host configuration only

- [ ] **Step 1: Run the default bootstrap**

Run: `sudo ./scripts/setup.sh`

Expected: Docker and NVIDIA Container Toolkit are installed and Docker restarts successfully.

- [ ] **Step 2: Verify default GPU injection**

Run: `docker compose run --rm joshua-u22 nvidia-smi`

Expected: The RTX 5080 and host driver are visible in the container.

- [ ] **Step 3: Verify real PyTorch CUDA computation**

Run the SO100 random-noise preset once to prepare its model environment, then execute a CUDA tensor multiplication from that environment.

Expected: `torch.cuda.is_available()` is true and the result tensor is on `cuda:0`.

- [ ] **Step 4: Verify CPU fallback**

Run the CPU overlay and confirm the container has no NVIDIA device reservation while the shell still starts successfully.

### Task 6: Regression verification and PR update

**Files:**
- Update: PR #57

- [ ] **Step 1: Validate all Compose combinations**

Validate base, dev, Isaac, and CPU configurations.

- [ ] **Step 2: Run CPU-backed test services**

Run `docker compose run --rm test-u22` and `docker compose run --rm test-u24`; both must pass all discovered Bazel tests without requesting a GPU.

- [ ] **Step 3: Run static checks**

Run Bash syntax checks and `git diff --check`.

- [ ] **Step 4: Commit any verification corrections**

Keep final corrections scoped to GPU-first Docker behavior.

- [ ] **Step 5: Push and update PR #57**

Push the branch, add GPU and CPU verification results to the PR description, and answer the three reviewer questions with canonical commands and the one-time host prerequisite.
