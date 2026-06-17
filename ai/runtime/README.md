# Inference runtime (engine)

The model-agnostic inference **engine**. This package never changes when
you add a model — concrete models live in [`ai/models/<model>/`](../models)
as self-contained plug-ins, wired in by [`ai/models/registry.py`](../models/registry.py).

## Architecture

```
ai/models/<model>/        ai/runtime/  (this package)
  config proto    ─────▶  ai_model.proto oneof
  adapter.py      ─────▶  registry ─▶ InferenceHost

ROS2 topics ─▶ InferenceHost (rclpy Node)
                 │  owns pub/sub, QoS, tick timer
                 │  decodes ROS msgs ─▶ Observation (numpy / float)
                 ▼
              InferenceAdapter (ROS-free plug-in, in ai/models/<model>/)
                 │  preprocess ─▶ inference ─▶ postprocess
                 ▼
              ActionCommand(s)
                 │
              InferenceHost  denormalizes + publishes ─▶ ROS2 topics
```

| File | Responsibility |
| --- | --- |
| `host.py` | `InferenceHost` ROS node: model selection, pub/sub, QoS, decode, dispatch, publish |
| `proto/runtime.proto` | Schema contracts: `ChannelRole`, `TriggerMode` enums and the `AdapterSpec` message |
| `types.py` | Re-exports the proto contracts and defines runtime dataclasses (`Observation`, `ActionCommand`, `ChannelSpec`) |
| `adapter.py` | `InferenceAdapter` base class (the plug-in contract) |
| `observation_codec.py` | Decodes ROS messages into canonical payloads |

> Schema-level contracts (the `ChannelRole`/`TriggerMode` enums and
> `AdapterSpec`) are defined in `proto/runtime.proto` and re-exported from
> `types.py`, so adapters still `from ai.runtime.types import ...`.
> `Observation`/`ActionCommand`/`ChannelSpec` stay as dataclasses because
> they carry live numpy/scalar payloads that are not protobuf-serialized.

### Why this split

- **Adapters are ROS-free.** The host decodes ROS messages into
  `Observation` objects and converts `ActionCommand` objects back to ROS
  messages. Adapters never import `rclpy` or ROS message packages, so
  they are trivial to unit test.
- **The host is model-free.** Adding a model never touches this package; it
  only delegates to the registry in `ai/models`.
- **Heavy deps stay lazy.** Adapter modules are imported on demand by the
  registry, so selecting `RANDOM_NOISE` never imports torch.

### Trigger modes

- `EVENT` (default): `on_observation` is called per decoded message; the
  adapter buffers/syncs internally and returns actions when ready.
- `TICK`: the host also calls `on_tick` at `AdapterSpec.tick_hz` for
  fixed-rate control loops.

## Adding a model

Models are plug-ins under `ai/models/<model>/`. See
[`ai/models/README.md`](../models/README.md) for the step-by-step guide.
No changes to this package are required.
