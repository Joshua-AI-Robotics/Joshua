# Compose-Native Tasks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Docker Compose own Joshua's build, test, and run workflows while keeping Make as optional shorthand.

**Architecture:** Add one-shot task services that inherit the existing runtime services and execute repository-owned container scripts. Make aliases select those services; CI invokes Compose directly. Documentation leads with Compose and shows Make only as optional shorthand.

**Tech Stack:** Docker Compose v2, Bash, Bazel, GNU Make, GitHub Actions

---

### Task 1: Define Compose task services

**Files:**
- Modify: `docker-compose.yml`
- Modify: `docker-compose.isaac.yml`
- Modify: `scripts/container_build.sh`

- [x] Add one-shot test, run, and x86/ARM64 build services for u22 and u24.
- [x] Parameterize launcher configuration and build target through environment variables with safe defaults.
- [x] Add Isaac-overlay launcher task services that inherit Isaac mounts and environment.
- [x] Run `docker compose config --services` for base, development, and Isaac configurations and confirm all task services resolve.

### Task 2: Thin the Make interface and CI

**Files:**
- Modify: `Makefile`
- Modify: `.github/workflows/ci.yml`

- [x] Replace embedded Bazel recipes with calls to named Compose task services.
- [x] Keep existing Make target names and parameter behavior for compatibility.
- [x] Change CI to invoke `docker compose run --rm test-u22` and `test-u24` directly.
- [x] Confirm `make help` labels Make as optional shorthand and grep the Makefile for embedded Bazel commands.

### Task 3: Update public instructions

**Files:**
- Modify: `README.md`
- Modify: `docs/GETTING_STARTED.md`
- Modify: `CONTRIBUTING.md`
- Modify: `scripts/README.md`
- Modify: `scripts/setup.sh`
- Modify: `ui/README.md`

- [x] Lead with direct Compose commands for shell, test, run, build, UI, and teardown workflows.
- [x] Show Make commands only as optional aliases.
- [x] Update bootstrap next steps so Docker remains the only required host tool.
- [x] Search public documentation for wording that makes Make mandatory.

### Task 4: Verify behavior

**Files:**
- Verify: `docker-compose.yml`
- Verify: `docker-compose.dev.yml`
- Verify: `docker-compose.isaac.yml`

- [x] Run base, development, and Isaac Compose config validation.
- [x] Run `docker compose run --rm test-u22` and expect all discovered Bazel tests to pass.
- [x] Run `docker compose run --rm test-u24` and expect all discovered Bazel tests to pass.
- [x] Run one packaged x86 build service and confirm artifacts appear under `dist/u22/x86`.
- [x] Run `git diff --check` and inspect the final diff for unrelated changes.
