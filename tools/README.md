# Tools

Standalone developer utilities. Nothing here is on the runtime path — the
launcher and ROS 2 nodes never invoke these.

## Contents

- `lock_model.sh` — regenerate the per-model `requirements.lock` for one
  inference model. Model venvs install the repo-root `requirements.lock` plus
  the model's own, so this file should list **only** packages not already at
  the root.

  ```bash
  tools/lock_model.sh smolvla
  ```

  Background on the per-model dependency scheme is in
  [ai/README.md](../ai/README.md).

## Responsibilities

- One-off developer and bring-up tasks that do not belong in a build rule.
- Regenerating checked-in artifacts (lockfiles) reproducibly.

## Non-Goals

- Anything the runtime depends on.
- Environment setup and build orchestration — those are
  [scripts/](../scripts/README.md).
- Firmware flashing. Planned to land here as `tools/flash/`, per
  [docs/BOARD_LAYER_RFC.md](../docs/BOARD_LAYER_RFC.md), but it does not exist
  yet and runtime code must never flash a board.
