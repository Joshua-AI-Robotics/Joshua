# AI stack

Joshua's AI code is organized around a **model-agnostic inference engine** and
**per-model plug-ins**. Config presets select which model runs on which node;
the engine never imports model-specific libraries directly.

| Package | Role |
| --- | --- |
| [`inference/`](inference/) | Inference engine: ROS host, launcher, manifests, per-model venvs |
| [`models/`](models/) | Model plug-ins: adapters, config protos, manifests, optional locks |
| [`proto/`](proto/) | Shared catalog (`ModelType`, `SingleModel`) |
| [`train/`](train/) | Data collection (`DataStore`) — separate from inference |

**Related docs**

- Model plug-ins (adapter contract, worked example): [`models/README.md`](models/README.md)
- Training roadmap: [`../docs/TRAINING_RFC.md`](../docs/TRAINING_RFC.md)

---

## Inference architecture

```
config preset (.pbtxt)
       │
       ▼
ros2/inference.py  ──▶  inference_launcher
       │                    reads config → ModelType
       │                    loads model.textproto manifest
       │                    [REEXEC] ensure_model_env → os.execve
       ▼
host_main  ──▶  InferenceHost (rclpy Node)
       │           decode ROS → Observation
       │           registry.create_adapter() → InferenceAdapter
       │           publish ActionCommand → ROS
       ▼
ai/models/<model>/adapter.py   (ROS-free plug-in)
```

**Engine vs plug-in**

- **Engine** (`ai/inference/`) owns ROS (pub/sub, QoS, decoding, scheduling).
  You do not edit it to add a model.
- **Plug-in** (`ai/models/<model>/`) owns model logic. Adapters receive
  `Observation` and return `ActionCommand`; they never import `rclpy`.

**Registry** (`ai/models/registry.py`) is manifest-driven: it loads
`ai/models/<model>/model.textproto` and imports the adapter entrypoint on
demand. You do not edit `registry.py` when adding a model.

---

## Per-model dependencies

Python can load only one version of a package per process. Models with heavy
or conflicting deps (torch, transformers, lerobot, …) must not share the main
inference process with other models.

Each model declares how it runs in `model.textproto`:

| `isolation` | When to use | Requirements | Runtime |
| --- | --- | --- | --- |
| `IN_PROCESS` | Lightweight, no conflicting deps | Optional — global `requirements.txt` only | Same process as launcher |
| `REEXEC` | Heavy or conflicting Python deps | `requirements.in` + `requirements.lock` | Launcher builds/syncs a cached venv and re-execs into `ai.inference.host_main` |

Example manifests:

```text
# random_noise — no extra pip closure
name: "random_noise"
model_type: RANDOM_NOISE
entrypoint: "ai.models.random_noise.adapter:RandomNoiseAdapter"
python_version: "3.10"
isolation: IN_PROCESS
```

```text
# smolvla — isolated venv from per-model lock
name: "smolvla"
model_type: SMOLVLA
entrypoint: "ai.models.smolvla.adapter:SmolVlaAdapter"
requirements_lock: "ai/models/smolvla/requirements.lock"
python_version: "3.10"
isolation: REEXEC
```

Venvs are cached at `~/.cache/joshua/envs/<model>-<lock-hash>`. First run may
take time (pip install). If a venv was built with stale paths, remove it:

```bash
rm -rf ~/.cache/joshua/envs/<model>-*
```

---

## Adding a model

### Path A — lightweight (`IN_PROCESS`)

Use when the adapter only needs packages already in the global lock (or no
extra pip deps beyond protobuf/numpy).

1. **Directory** — `ai/models/<model>/` with:
   - `<model>_config.proto` — per-model config message
   - `adapter.py` — `InferenceAdapter` subclass
   - `model.textproto` — manifest (`IN_PROCESS`, entrypoint)
   - `BUILD` — proto targets + `adapter` `py_library`

2. **Catalog** — in `ai/proto/ai_model.proto`:
   - new `ModelType` enum value
   - new `oneof model_config` field on `SingleModel`
   - add proto dep in `ai/proto/BUILD`

3. **Aggregate** — in `ai/models/BUILD`:
   - add `//ai/models/<model>:adapter` to `sources`
   - add `//ai/models/<model>:model.textproto` to `manifests`

4. **Preset** — `config/config_preset/.../*.pbtxt` with an
   `ai.models.single_models` entry for the inference node.

5. **Verify**

   ```bash
   bazel build //ai/models/<model>:adapter //ros2:inference
   ./hooks/lint_check.sh --fix
   ```

Reference: [`ai/models/random_noise/`](models/random_noise/).

### Path B — heavy deps (`REEXEC`)

Everything in Path A, plus:

6. **Requirements**
   - `ai/models/<model>/requirements.in` — direct pins
   - `tools/lock_model.sh <model>` → `requirements.lock` (+ `requirements_3.12.lock`)

7. **Bazel pip hub** — in `MODULE.bazel`, register per-model hubs (mirror
   `joshua_pip_smolvla` / `joshua_pip_smolvla_312`):

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

8. **Adapter BUILD deps** — use `model_requirement` from
   [`ai/models/requirements.bzl`](models/requirements.bzl):

   ```python
   load("//ai/models:requirements.bzl", "model_requirement")

   deps = [
       "//ai/inference:adapter",
       "//ai/inference:types",
   ] + model_requirement("<model>", "torch")
   + model_requirement("<model>", "transformers")
   ```

   Keep heavy deps on the model's `adapter` target only — not on shared
   `registry` or `host`.

9. **Launcher runfiles** — add the lock to `ai/inference/BUILD`
   `inference_launcher` `data`:

   ```python
   data = [
       "//ai/models:sources",
       "//ai/models:manifests",
       "//ai/models/<model>:requirements.lock",
   ]
   ```

10. **Manifest** — set `requirements_lock` and `isolation: REEXEC` in
    `model.textproto`.

Reference: [`ai/models/smolvla/`](models/smolvla/).

---

## Checklists

### Every model

- [ ] `ai/models/<model>/<model>_config.proto`
- [ ] `ai/models/<model>/adapter.py` (`spec()`, `on_observation()` minimum)
- [ ] `ai/models/<model>/model.textproto` (entrypoint + isolation)
- [ ] `ai/models/<model>/BUILD` (proto + adapter; deps scoped to adapter)
- [ ] `ai/proto/ai_model.proto` — `ModelType` + oneof field
- [ ] `ai/proto/BUILD` — proto dep on new config
- [ ] `ai/models/BUILD` — `sources` + `manifests` filegroups
- [ ] Config preset with `single_models` entry
- [ ] `bazel build //ros2:inference`

### Additional for `REEXEC` models

- [ ] `requirements.in` + `tools/lock_model.sh <model>`
- [ ] `MODULE.bazel` — `joshua_pip_<model>` hubs
- [ ] `model_requirement("<model>", ...)` in adapter BUILD
- [ ] `//ai/models/<model>:requirements.lock` in `inference_launcher` data

---

## Bazel vs runtime deps

| Layer | Purpose |
| --- | --- |
| **Bazel** (`adapter` BUILD + pip hub) | Compile-time imports, tests, type-checking in the monorepo |
| **Runtime** (`requirements.lock` + venv) | What actually runs inside the model process after `REEXEC` |

Both must list the packages your adapter imports. The global
`requirements.txt` / `requirements.lock` should stay slim; model-heavy pins
live in per-model locks.

---

## Data collection (not inference)

Recording demos uses `ai/train/data_store.py` and is independent of the
inference plug-in system. See [`train/README.md`](train/README.md).

Training (BC, RL) is planned under `ai/data/` and `ai/training/` per
[`docs/TRAINING_RFC.md`](../docs/TRAINING_RFC.md) — not implemented yet.
