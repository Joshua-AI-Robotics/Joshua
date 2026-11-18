# Web UI

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../docs/CODING_GUIDELINES.md). Read the local
[README.md](README.md) before editing. This file owns the rules and architecture
contract for `ui/`; child directories inherit it unless they add local rules.

## Architecture contract

React views own presentation and user interaction. Shared clients adapt existing
ROS/bridge/config contracts; drivers, device probing, static config acceptance,
and runtime lifecycle authority remain in their owning subsystems. Use the
protobuf-derived schema instead of duplicating hardware declarations in TypeScript.

Keep effects and subscriptions scoped to the owning component/client with explicit
cleanup. Reuse existing components and clients before adding state managers or
parallel data pipelines. Do not turn stale feedback or UI connection state into
hardware readiness or an automatic enable/recovery action.

## Verification

Use the Docker build/dev entrypoints in the README. Run the package's lint and
TypeScript/build checks for UI changes, and exercise affected interactions using
synthetic bridge responses. Include screenshots or behavior evidence for visible
changes. Do not send commands to real robots to verify the UI.
