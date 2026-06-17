# Models (inference plug-ins)

Each model is a **self-contained plug-in** in its own directory holding
both its config schema and its adapter implementation. The model-agnostic
engine lives in [`ai/runtime`](../runtime); this package is the set of
plug-ins plus the single registry that wires them in.

## Layout

```text
ai/models/
  registry.py            # ModelType -> adapter wiring (the one aggregation point)
  smolvla/
    smolvla_config.proto # config schema
    adapter.py           # SmolVlaAdapter
    BUILD                # proto targets + adapter py_library (heavy deps)
  random_noise/
    random_noise_config.proto
    adapter.py           # RandomNoiseAdapter
    BUILD
```

| Concern | Location |
| --- | --- |
| Model selection enum | `ai/proto/ai_model.proto` (`ModelType`) + `SingleModel` oneof |
| Per-model config message | `ai/models/<model>/<model>_config.proto` |
| Per-model adapter | `ai/models/<model>/adapter.py` |
| Registry (wiring) | `ai/models/registry.py` |
| Engine (host, contracts) | `ai/runtime/` |

## How to add a model

Adding a model touches only this package and the `ai_model.proto` catalog —
never the engine. In short:

1. Create `ai/models/<model>/` with `<model>_config.proto` + `adapter.py` +
   `BUILD`.
2. Register it in the catalog: new `ModelType` value and `oneof` field in
   `ai/proto/ai_model.proto` (+ proto dep in `ai/proto/BUILD`).
3. Wire it into `ai/models/registry.py` (lazy loader + `ADAPTER_LOADERS`)
   and add the dep in `ai/models/BUILD`.
4. Add an `ai.models.single_models` entry in a config preset.

`//ros2:inference` then picks it up transitively via the host.

> **Full walkthrough:** [`ADDING_AN_ADAPTER.md`](ADDING_AN_ADAPTER.md) — an
> extremely detailed, from-scratch guide with the complete adapter
> contract, the host lifecycle/call order, trigger modes, a full worked
> example (a `TICK`-mode EMA smoother), publishing/normalization
> semantics, threading notes, unit-testing without ROS, common pitfalls,
> and a final checklist.
