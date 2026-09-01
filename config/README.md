# Config

The protobuf config is the **single source of truth** for a robot setup. One
`.pbtxt` file describes the whole system — actuators, boards, sensors, AI
policies, simulation backend — and the launcher instantiates ROS 2 nodes from
it. If a value describes a particular robot, it belongs here, not in code.

```bash
bazel run //launcher:joshua_main -- --config config/config_preset/so100/sim_passive.pbtxt
```

> A preset may drive **real hardware** whatever it is named and whatever
> `operation_mode` it declares. Read the preset before running it — see the
> hardware-safety section of [AGENTS.md](../AGENTS.md).

## Layout

- `proto/` — the schema.
  - `robot.proto` — top-level `Robot`; composes the others.
  - `config.proto` — `General`, including `operation_mode`, which the launcher
    branches on.
  - `ai.proto` — inference and policy configuration.
  - `physical_layout.proto` — poses, axes, and frames.
- `config_preset/<robot>/` — checked-in presets, one per robot and task
  (`so100/`, `example/`).
- `config_utils.h` — `LoadConfig`, which opens a `.pbtxt` and parses it into a
  `config::Config`. Parsing only; semantic checks happen later, in the
  factories and `node_generator/`.

## Responsibilities

- Describe *what* a robot is made of and how it is wired.
- Stay the only place a robot-specific value appears: ports, joint limits, gear
  ratios, servo IDs, topic names, model paths.
- Carry enough structure that an inconsistent setup can be rejected during
  startup — in the factories and `node_generator/` — rather than at first
  motion.

## Non-Goals

- Behavior. How a motor moves is driver logic in [robot/](../robot/README.md).
- Secrets or machine-local paths. Presets are committed and shared.
- Generated artifacts. Do not hand-edit anything produced by codegen.

## Adding or changing a preset

1. Start from the nearest existing preset in the same robot directory.
2. Change the schema in `proto/` only if the field genuinely does not exist —
   a new preset should rarely need one.
3. For anything you intend to run repeatedly during development, pick a preset
   you have read and confirmed declares no real devices. Neither the filename
   nor `operation_mode: MODE_SIMULATION` is that confirmation — both cross the
   safe/unsafe boundary in this repo. What counts as safe is defined once, in
   the hardware-safety section of [AGENTS.md](../AGENTS.md); check the preset
   against that list rather than against a copy of it here.
