# Inference engine

Model-agnostic ROS2 inference host and launcher. Concrete models live in
[`ai/models/<model>/`](../models); this package does not import them directly.

**Adding a model:** [`../README.md`](../README.md) (workflow and checklists).
**Adapter API and worked example:** [`../models/README.md`](../models/README.md).

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
| `inference_launcher.py` | Read config → manifest → optional `REEXEC` into model venv |
| `environment_manager.py` | Create/sync cached venv from per-model lock |
| `host_main.py` | Post-re-exec entry: ROS paths + `InferenceHost` |
| `host.py` | `InferenceHost` node |
| `manifest.py` | Load `model.textproto`, resolve workspace paths |
| `adapter.py` | `InferenceAdapter` base class |
| `types.py` | `Observation`, `ActionCommand`; re-exports proto enums |
| `observation_codec.py` | ROS message → canonical payloads |
| `proto/inference.proto` | `ChannelRole`, `TriggerMode`, `AdapterSpec` |
| `proto/model_manifest.proto` | `ModelManifest`, `Isolation` enum |

## Trigger modes

- `EVENT` — `on_observation` per decoded message; adapter buffers internally.
- `TICK` — host also calls `on_tick` at `AdapterSpec.tick_hz`.
