# Shared utilities

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../AGENTS.md#coding-guidelines). This file owns the rules and architecture
contract for `utils/`; child directories inherit it unless they add local rules.

## Architecture contract

Utilities own focused reusable operations: clock conversion, scope cleanup, and
process failure boundaries. They do not own robot graphs, ROS executors, shared
buses, or driver lifecycles. Keep the common process runtime ROS independent;
ROS-specific adapters belong under `ros2/`.

Reuse `joshua_runtime` and scope guards for exceptions, worker reporting, and
cleanup. A worker's caller owns joining it, including rollback if later setup
throws. Never turn a clock failure into a plausible timestamp or confuse device,
RobotTime, Unix, and monotonic clock domains.

## Verification

Run the affected tests in [BUILD](BUILD) using controlled clocks, temporary
resources, and child processes. Exercise failure boundaries and cleanup, not
only successful returns. Physical input or motor motion is not required.

## Shared utilities

Small shared libraries provide process failure handling, scope cleanup, RobotTime,
and logging helpers. See [AGENTS.md](AGENTS.md) for ownership
and dependency constraints, and [RobotTime](../docs/ROBOT_TIME.md) for clock semantics.

Standalone controller input lives under
[tools/teleop/xbox_controller/](../tools/teleop/xbox_controller/AGENTS.md).
