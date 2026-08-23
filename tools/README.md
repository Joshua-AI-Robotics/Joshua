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

- `pybricks/` — host-side Pybricks/SPIKE bring-up tooling: `pybricks_driver.py`,
  its BLE transport, and a `pybricks_ble_smoke` binary that drives one motor.

  ```bash
  bazel run //tools/pybricks:pybricks_ble_smoke -- SPIKE A 90
  ```

  This talks to a real hub over Bluetooth and moves a real motor. See the
  hardware-safety section of [AGENTS.md](../AGENTS.md).

  It lives here rather than under `robot/` on purpose. The Python robot layer
  was removed in [docs/BOARD_LAYER_RFC.md](../docs/BOARD_LAYER_RFC.md) §10
  Phase 9 and `robot/` is C++ only. **This tool is now the only way to drive a
  SPIKE hub.** The `SPIKE_HUB_BLE` board and the `MOTOR_SPIKE` motor type were
  both removed, so no preset can reach a hub through the launcher. The
  `SPIKE_MOTOR` actuator type and `SpikeMotorConfig` remain in
  `robot/action/proto/action.proto` because this tool builds an `Actuator`
  message from them.

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
