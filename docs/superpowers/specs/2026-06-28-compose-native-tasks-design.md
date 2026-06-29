# Compose-Native Tasks Design

## Goal

Make Docker Compose the canonical interface for Joshua development workflows while retaining Make as an optional shorthand.

## Design

One-shot Compose services will own test, launcher, and packaged-build commands for Ubuntu 22/Humble and Ubuntu 24/Jazzy. Each task service inherits the matching Joshua image, mounts, device access, and Bazel cache from its runtime service. Build services will also cover x86 and ARM64 targets.

The Makefile will contain aliases only. It will select a named Compose service and pass user inputs through environment variables; it will not contain Bazel commands or build logic. GitHub Actions will invoke the Compose test services directly so CI does not depend on Make.

The existing UI services remain canonical because they already run directly through Compose. Isaac commands continue to use the Isaac overlay, with dedicated task services in that overlay so mounted Isaac paths are preserved.

## Public Interface

- `docker compose run --rm test-u22`
- `docker compose run --rm test-u24`
- `CONFIG=... docker compose run --rm run-u22`
- `CONFIG=... docker compose run --rm run-u24`
- `TARGET=... docker compose run --rm build-u22-x86`
- `TARGET=... docker compose run --rm build-u24-arm64`
- Existing `make` commands remain optional aliases for these services.

## Verification

Validate every Compose overlay, confirm Make recipes contain no Bazel commands, run both x86 test services, smoke the packaged-build service, and check that CI and public documentation use direct Compose commands as the canonical path.
