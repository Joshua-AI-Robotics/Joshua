# Setup and build scripts

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../docs/CODING_GUIDELINES.md). Read the local
[README.md](README.md) before editing. This file owns the rules and architecture
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
