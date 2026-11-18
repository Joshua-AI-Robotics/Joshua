# AI and data pipelines

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../docs/CODING_GUIDELINES.md). This file owns the rules and architecture
contract for `ai/`; child directories inherit it unless they add local rules.

## Architecture contract

Inference adapters own model-specific loading and observation handling. The
shared host/registry owns model discovery and dispatch; add adapters through the
existing manifest/spec interface instead of model switches in the shared host.
Use ROS topics and existing recording interfaces for data flow. Training/demo
recording remains separate from runtime inference and hardware ownership.

Keep model-specific dependencies on that model's adapter target and lockfiles.
Shared host/registry targets use shared dependencies only. Both Bazel imports
and runtime venv locks must cover the adapter's imports. Never move robot setup
or device ownership into a model adapter.

## Adding a model

1. **Directory** — `ai/models/<model>/` with:
   - `<model>_config.proto` — per-model config message
   - `adapter.py` — `InferenceAdapter` subclass
   - `model.textproto` — entrypoint; optional `requirements_lock`
   - `BUILD` — proto targets + `adapter` `py_library`
   - `requirements.in` — only if the model needs deps beyond the global base

2. **Locks** (when needed) — `tools/lock_model.sh <model>` for model-specific
   `requirements.lock` / `requirements_3.12.lock`

3. **Catalog** — in `ai/proto/ai_model.proto`:
   - new `ModelType` enum value
   - new `oneof model_config` field on `SingleModel`
   - add proto dep in `ai/proto/BUILD`

4. **Bazel pip hub** — in `MODULE.bazel` (mirror existing models):

   ```python
   pip.parse(
       hub_name = "joshua_pip_<model>",
       python_version = "3.10",
       requirements_lock = "//ai/models/<model>:requirements.lock",
   )
   pip.parse(
       hub_name = "joshua_pip_<model>_312",
       python_version = "3.12",
       requirements_lock = "//ai/models/<model>:requirements_3.12.lock",
   )
   use_repo(pip, "joshua_pip_<model>")
   use_repo(pip, "joshua_pip_<model>_312")
   ```

5. **Adapter BUILD deps** — global base via `requirement()`; model-only via
   `model_requirement()` (only when the model has a pip hub):

   ```python
   load("@project_joshua_pip_deps//:requirements.bzl", "requirement")
   load("//ai/models:requirements.bzl", "model_requirement")

   deps = [
       "//ai/inference:adapter",
       "//ai/inference:types",
       requirement("protobuf"),
   ] + model_requirement("smolvla", "torch")  # model-specific only
   ```

   Keep deps on the model's `adapter` target only — not on shared `host` or
   `registry`.

6. **Aggregate** — in `ai/models/BUILD`:
   - add `//ai/models/<model>:adapter` to `sources`
   - add `//ai/models/<model>:model.textproto` to `manifests`
   - add `//ai/models/<model>:requirements.lock` to `locks`

7. **Preset** — `config/config_preset/.../*.pbtxt` with a `single_models`
   entry for the inference node.

8. **Verify**

   ```bash
   bazel build //ai/models/<model>:adapter //ros2:inference
   ./hooks/lint_check.sh --fix
   ```

References: [`random_noise/`](models/random_noise/) (base venv only),
[`smolvla/`](models/smolvla/) (base + model lock).


Run adapter/host tests and build the changed adapter plus `//ros2:inference` in
Docker. Loading a model or replaying data does not authorize motor commands.

## AI stack

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

- Model plug-ins (adapter contract, worked example): [`models/AGENTS.md`](models/AGENTS.md)

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
inference plug-in system. See [`train/AGENTS.md`](train/AGENTS.md).

Training (BC, RL) is not implemented. The former RL pipeline and the training
RFC that described its planned replacement have been removed.

## Directory navigation

- [inference/](inference/AGENTS.md)
- [models/](models/AGENTS.md)
- [train/](train/AGENTS.md)
