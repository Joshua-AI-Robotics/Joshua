# Inference engine

Follow the repository [instructions](../../AGENTS.md) and applicable ancestor
`AGENTS.md` files before changing or running this subsystem.

Model-agnostic ROS2 inference host and launcher. Concrete models live in
[`ai/models/<model>/`](../models); this package does not import them directly.

**Adding a model:** [`../AGENTS.md`](../AGENTS.md) (workflow and checklists).
**Adapter API and worked example:** [`../models/AGENTS.md`](../models/AGENTS.md).

## Architecture

```
ai/models/<model>/        ai/inference/  (this package)
  model.textproto ─────▶  manifest.load_manifest()
  adapter.py      ─────▶  registry ─▶ InferenceHost

ROS2 topics ─▶ InferenceHost (rclpy Node)
                 │  pub/sub, QoS, decode → Observation
                 ▼
              InferenceAdapter (ROS-free, in ai/models/<model>/)
                 ▼
              ActionCommand(s) ─▶ host publishes to ROS
```

## Key files

| File | Role |
| --- | --- |
| `inference_launcher.py` | Read config → manifest → build venv → re-exec |
| `environment_manager.py` | Sync global base + optional model lock into venv |
| `host_main.py` | Post-re-exec entry: ROS paths + `InferenceHost` |
| `host.py` | `InferenceHost` node |
| `manifest.py` | Load `model.textproto`, resolve workspace paths |
| `adapter.py` | `InferenceAdapter` base class |
| `types.py` | `Observation`, `ActionCommand`; re-exports proto enums |
| `observation_codec.py` | ROS message → canonical payloads |
| `proto/inference.proto` | `ChannelRole`, `TriggerMode`, `AdapterSpec` |
| `proto/model_manifest.proto` | `ModelManifest` (entrypoint, optional lock path) |

## Trigger modes

- `EVENT` — `on_observation` per decoded message; adapter buffers internally.
- `TICK` — host also calls `on_tick` at `AdapterSpec.tick_hz`.

## Motor-policy replay

The optional `ai.motor_policy` preset field defines the version 1 replay contract.
Each question selects required RGB8, text or JSON evidence by name and maps each
answer to an absolute joint position, a measured-position increment, or an explicit
no-command outcome. Version 1 supports one revolute joint per question, in radians.
A question with an incremental choice requires measured position in every request.
The configuration validator checks names, references, mode, units and limits.

After validation, `motor_policy.evaluate_choice` checks a request, calls a
fixed-choice provider, and returns the original request and sample identities with
either a named SI `JointCommand` or a reason for producing no command. The replay
fixture uses a rotary gripper and an in-memory sink. Run it with
`bazel test //ai/inference:motor_policy_test //config:validation_test` in a dev shell.
The scalar ROS host only accepts legacy adapter specs. Asynchronous scheduling,
deadline enforcement and hardware dispatch belong to later integration.
