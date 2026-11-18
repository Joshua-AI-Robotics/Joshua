# Shared utilities

Small shared libraries provide process failure handling, scope cleanup, RobotTime,
logging helpers, and controller input. See [AGENTS.md](AGENTS.md) for ownership
and dependency constraints, and [RobotTime](../docs/ROBOT_TIME.md) for clock semantics.

Hardware input utilities can open devices; tests must use controlled clocks and
fake inputs under the root hardware authorization rules.
