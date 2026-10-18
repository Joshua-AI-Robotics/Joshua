# <Subsystem>

One paragraph: what this subsystem is and where it sits in Joshua — what it
depends on and what depends on it. Link the neighbours (e.g. "ROS 2 nodes live
in [ros2/](../ros2/README.md) and consume these interfaces").

<!-- If anything here can open a real device or move a motor, say so plainly and
link the hardware-safety section of ../AGENTS.md. Do not imply a path is
simulation-only unless you have confirmed it. -->

## Contents

- `<file-or-dir>` — what it is and when someone touches it.

  ```bash
  # the one command that exercises it, if there is one
  ```

## Responsibilities

- What this subsystem is meant to own.

## Non-Goals

- What deliberately lives elsewhere, with a link (e.g. build orchestration is
  [scripts/](../scripts/README.md)).

<!--
Delete any section that does not apply rather than leaving it empty.
Keep it to what a human needs; agent-operational deltas (build/test
invocations, "never do X here") belong in a nested AGENTS.md, not here.
See ../docs/AGENTS_RFC.md §7.
-->
