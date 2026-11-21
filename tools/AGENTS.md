# Developer tools

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../AGENTS.md#coding-guidelines). This file owns the rules and architecture
contract for `tools/`; child directories inherit it unless they add local rules.

## Architecture contract

Tools adapt existing subsystem contracts; production runtime must not depend
on these utilities. Use protobuf configuration and factory-owned discovery
metadata rather than a parallel device registry, validation implementation,
message bus, or lifecycle manager. Recording tools consume ROS data and preserve
source metadata, validity, loss, and clock domains.

Use the nearest existing utility before introducing a wrapper. Keep model lock
regeneration reproducible and separate from runtime loading. Python bench drivers
stay here rather than in the C++ robot layer. A tool capable of device I/O or
motion follows the root hardware authorization rule.

The [MCP contract](mcp/AGENTS.md) owns read-only adapter boundaries. Test changed
tools with their existing Bazel targets in Docker; use synthetic ROS messages,
fake helpers, and temporary directories. Never substitute a physical bench run
for automated verification.

## Tools

Standalone developer utilities. Nothing here is on the runtime path — the
launcher and ROS 2 nodes never invoke these.

## Contents

- [hardware_catalog.py](hardware_catalog.py) — shared hardware record validation,
  one device file, embedded validation history, integrity checks and bounded queries. The [catalog contract](../docs/hardware_catalog/AGENTS.md) owns its
  templates and contribution workflow; MCP is a consumer of this tool.

- [`serial/record_scalar.py`](serial/record_scalar.py) — bounded, read-only ROS
  scalar/health recording to JSONL. It retains the scalar fields and `sample_metadata`,
  including device acquisition ticks and separate host receipt clocks. It opens
  no board and sends no commands. Observation subscriptions use
  best-effort delivery to accept either publisher reliability setting; captures
  may contain gaps. See the
  [serial qualification procedure](../docs/SERIAL_COMM_QUALIFICATION.md).

- [`mcp/`](mcp/AGENTS.md) — ten read-only stdio MCP tools for config-free factory
  discovery, curated hardware models, separate historical hardware test records,
  per-call protobuf validation and device inspection, and optional private
  runtime snapshots. The adapter never controls runtime or certifies hardware
  readiness; validation-only startup remains available.

- `lock_model.sh` — regenerate the per-model `requirements.lock` for one
  inference model. Model venvs install the repo-root `requirements.lock` plus
  the model's own, so this file should list **only** packages not already at
  the root.

  ```bash
  tools/lock_model.sh smolvla
  ```

  Background on the per-model dependency scheme is in
  [ai/AGENTS.md](../ai/AGENTS.md).

- `pybricks/` — host-side Pybricks/SPIKE bring-up tooling: `pybricks_driver.py`,
  its BLE transport, and a `pybricks_ble_smoke` binary that drives one motor.

  ```bash
  bazel run //tools/pybricks:pybricks_ble_smoke -- SPIKE A 90
  ```

  This talks to a real hub over Bluetooth and moves a real motor. See the
  hardware-safety section of [AGENTS.md](../AGENTS.md).

  It lives here rather than under `robot/` on purpose. The Python robot layer
  was removed; `robot/` is C++ only. **This tool is now the only way to drive a
  SPIKE hub.** The `SPIKE_HUB_BLE` board and the `MOTOR_SPIKE` motor type were
  both removed, so no preset can reach a hub through the launcher. Nothing Spike-shaped
  survives outside this directory: `ActuatorType::SPIKE_MOTOR`,
  `SpikeMotorConfig`, and `CommType::BLE` are all gone from the robot protos,
  and the tool configures itself with its own `SpikeMotorSpec` dataclass.

## Responsibilities

- One-off developer and bring-up tasks that do not belong in a build rule.
- Regenerating checked-in artifacts (lockfiles) reproducibly.

## Non-Goals

- Anything the runtime depends on.
- Environment setup and build orchestration — those are
  [scripts/](../scripts/AGENTS.md).
- Firmware flashing. Board-specific procedures belong in
  [firmware/AGENTS.md](../firmware/AGENTS.md); runtime code never flashes a board.

## Directory navigation

- [mcp/](mcp/AGENTS.md)
