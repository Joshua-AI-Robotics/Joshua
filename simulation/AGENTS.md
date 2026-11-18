# Simulation

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../docs/CODING_GUIDELINES.md). Read the local
[README.md](README.md) before editing. This file owns the rules and architecture
contract for `simulation/`; child directories inherit it unless they add local rules.

## Architecture contract

`main.py` selects the configured backend. MuJoCo owns in-process physics;
Isaac's launcher owns serialization into its separate Python environment and
viewer process. The existing launcher NodeManager remains graph/process owner.
Do not add another graph supervisor or move backend selection into robot drivers.

Keep model assets under `models/` and setup in protobuf. Preserve ROS topics,
units, frames, timestamps, and explicit simulator stepping semantics. A mirror
mode can consume real hardware; inspect every configured endpoint before running.

## Verification

Use unit tests and headless scenes for the changed backend in its documented
container environment. Report GPU/Isaac requirements and skipped checks. Simulated
behavior does not certify real motor limits, device readiness, or stop latency.
