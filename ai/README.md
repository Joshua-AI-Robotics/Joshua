# AI stack

Implementation rules and the architecture contract live in [AGENTS.md](AGENTS.md).

Joshua's AI code is organized around a **model-agnostic inference engine** and
**per-model plug-ins**. Config presets select which model runs on which node;
the engine never imports model-specific libraries directly.

| Package | Role |
| --- | --- |
| [`inference/`](inference/) | Inference engine: ROS host, launcher, per-model venvs |
| [`models/`](models/) | Model plug-ins: adapters, config protos, manifests, locks |
| [`proto/`](proto/) | Shared catalog (`ModelType`, `SingleModel`) |
| [`train/`](train/) | Data collection (`DataStore`) — separate from inference |

**Related docs**

- Model plug-ins (adapter contract, worked example): [`models/README.md`](models/README.md)

---

## Inference architecture

```
config preset (.pbtxt)
       │
       ▼
ros2/inference.py  ──▶  inference_launcher
       │                    reads config → ModelType
       │                    loads model.textproto manifest
       │                    ensure_model_env → os.execve
       ▼
host_main  ──▶  InferenceHost (rclpy Node)
       │           decode ROS → Observation
       │           registry.create_adapter() → InferenceAdapter
       │           publish ActionCommand → ROS
       ▼
ai/models/<model>/adapter.py   (ROS-free plug-in, in model venv)
```

**Engine vs plug-in**

- **Engine** (`ai/inference/`) owns ROS (pub/sub, QoS, decoding, scheduling).
  You do not edit it to add a model.
- **Plug-in** (`ai/models/<model>/`) owns model logic in a **dedicated venv**.
  Adapters receive `Observation` and return `ActionCommand`; they never
  import `rclpy`.

**Registry** (`ai/models/registry.py`) is manifest-driven: it loads
`ai/models/<model>/model.textproto` and imports the adapter entrypoint on
demand. You do not edit `registry.py` when adding a model.

---

## Per-model dependencies

Every model runs in its own venv. The launcher always re-execs into a cached
environment built from **two layers**:

1. **Global base** — `requirements.lock` / `requirements_3.12.lock` at repo root.
2. **Model-specific** — optional `ai/models/<model>/requirements.lock`, installed
   **after** the base so model pins can override (e.g. SmolVLA's `Pillow==11.2.1`).

```text
name: "smolvla"
model_type: SMOLVLA
entrypoint: "ai.models.smolvla.adapter:SmolVlaAdapter"
requirements_lock: "ai/models/smolvla/requirements.lock"
python_version: "3.10"
```

Models with no extra pip deps (e.g. `random_noise`) omit `requirements_lock`;
their venv is the global base only.

`requirements.in` under each model should list **only** packages not already
in the root `requirements.txt`. Regenerate with `tools/lock_model.sh <model>`.

On Python 3.12, `requirements.lock` in the manifest maps to
`requirements_3.12.lock` in the same directory when that file exists.

Venv cache key hashes **base lock + model lock**. Cached at
`~/.cache/joshua/envs/<model>-<fingerprint>`. Clear stale envs:

```bash
rm -rf ~/.cache/joshua/envs/<model>-*
```

---

## Adding a model

Follow the model registration and verification procedure in [AGENTS.md](AGENTS.md#adding-a-model).

## Bazel vs runtime deps

| Layer | Purpose |
| --- | --- |
| **Bazel** | Global `requirement()` + optional `model_requirement()` for model hub |
| **Runtime** | `pip install -r requirements.lock [-r model.lock]` into model venv |

Both layers must cover what the adapter imports. Global pins stay in the root
lock; model locks hold only model-specific closures.

---

## Data collection (not inference)

Recording demos uses `ai/train/data_store.py` and is independent of the
inference plug-in system. See [`train/README.md`](train/README.md).

Training (BC, RL) is not implemented. The former RL pipeline and the training
RFC that described its planned replacement have been removed.
