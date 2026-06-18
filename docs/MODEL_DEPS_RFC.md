# Model Dependency Isolation RFC

Status: Draft (design + immediate fix)
Companion to: [AI_PIPELINE_RFC.md](AI_PIPELINE_RFC.md), [TRAINING_RFC.md](TRAINING_RFC.md)

## 1. Purpose

Define how Joshua manages **per-model Python dependencies** so that adding a
new AI model can never break the dependency resolution of an existing one.

The trigger: running SmolVLA inference fails with

```
ValueError: Could not find module SmolVLMImageProcessor in `transformers`
```

which is a *masked* symptom of a hard version conflict in the dependency
graph (`lerobot` ↔ `transformers` ↔ `Pillow`). The error is not in Joshua
code — it is a structural problem with how the repo pins dependencies. This
RFC fixes the immediate bug and lays out the architecture that prevents the
whole class of problem.

## 2. The problem, precisely

### 2.1 One global closure

```
MODULE.bazel
  pip.parse(hub_name = "project_joshua_pip_deps",
            requirements_lock = "//:requirements.lock")   # ONE lock
```

Every adapter target pulls `requirement(...)` from this single hub:

```
ai/models/smolvla/BUILD
  py_library(name = "adapter", deps = [
      requirement("transformers"), requirement("lerobot"),
      requirement("pillow"), requirement("num2words"), ... ])
```

So a single `pip-compile` must simultaneously satisfy **every** model. With
two models this is annoying; with N heavy VLA/RL models it becomes an
unsolvable diamond (model A needs `transformers==4.53.*`, model B needs
`transformers>=5.4`, etc.).

### 2.2 One process

`ros2/inference` is a single binary. Its registry lazily imports whichever
adapter the config selects, but **all adapters are dependencies of the one
binary**, so all their packages land in one runfiles tree and one Python
interpreter:

```
ros2/BUILD: ros2_py_binary(name="inference", deps=["//ai/runtime:host", ...])
ai/runtime/host.py: from ai.models import registry  ->  imports adapters
```

A Python process can hold exactly **one** version of any package. Even if we
split BUILD targets or pip hubs, two models with conflicting versions can
never coexist *in the same process*. This is the key constraint that drives
the whole design: **true isolation must happen at the process/environment
boundary, not in-process.**

### 2.3 The concrete conflict (worked example)

- `lerobot==0.4.1` caps `transformers<5.0.0` and (smolvla extra) requires
  `num2words>=0.5.14`. Our lock pins `num2words==0.5.13` — already a
  violation.
- Every `transformers` 4.x (≤4.57.1, the latest 4.x) annotates, at import
  time, `palette: Optional[PIL.ImagePalette.ImagePalette]` in
  `models/smolvlm/image_processing_smolvlm.py`.
- `Pillow==11.3.0` (our pin) stopped eagerly importing the `ImagePalette`
  submodule; `Pillow==11.2.1` still does. So `PIL.ImagePalette` is undefined
  at import → `AttributeError`, which `transformers`' lazy loader swallows
  and re-raises as the misleading "Could not find module ..." message.

Because `transformers` is capped `<5.0.0` by `lerobot`, **bumping
transformers cannot fix it**. The fix must pin `Pillow==11.2.1` (and
`num2words>=0.5.14`) *for SmolVLA*. Under one global lock, that pin is forced
on every other model — exactly the coupling we want to remove.

## 3. The durable design: contract + manifest + agnostic host

The packaging mechanism is replaceable; the **seam** is what makes the system
modular. Joshua already has most of it:

| Layer | What it is | Status |
|-------|-----------|--------|
| Adapter contract | ROS-free `InferenceAdapter` (`spec/validate/initialize/on_observation/on_tick/close`) | ✅ exists (`ai/runtime/adapter.py`) |
| Model-agnostic host | `InferenceHost` owns all ROS, knows no model | ✅ exists (`ai/runtime/host.py`) |
| Registry | `ModelType -> adapter` lazy map | ✅ exists (`ai/models/registry.py`) |
| **Manifest** | declarative `ModelType -> adapter -> deps -> isolation` | ➕ proposed here |

If those four are clean, **dependency isolation becomes a strategy you choose
per model**, swappable without touching any adapter. The rest of this RFC is
the menu of isolation strategies and exact implementation for each, plus when
to use which.

## 4. Decision criteria

Use the least isolation that works. Escalate only when a constraint forces it.

```
Do any two models have incompatible pins (same package, disjoint ranges)?
  │
  ├─ No  ───────────────────────────────────► Option 0 (shared lock)
  │
  └─ Yes
      │ Do they share the same Python minor version (== system ROS, 3.10)?
      │
      ├─ Yes ──────────────────────────────► Option 3 (re-exec into per-model venv)
      │
      └─ No  (different Python, or non-Python, or unfixable C-ext clash)
              └──────────────────────────────► Option 4 (subprocess worker)
                                               or Option 5 (container) at scale
```

Supporting facts that constrain the choices:
- `rclpy` is **not** on PyPI; it comes from system ROS via `PYTHONPATH` and is
  ABI-locked to the system Python (3.10 on Humble / 3.12 on Jazzy). Any env
  that uses `rclpy` directly must match that Python minor version.
- Heavy models pull ~1–3 GB of wheels (torch, CUDA). N full environments cost
  real disk; relevant for edge/robot deployment.

## 5. Options ladder (full implementation detail)

### Option 0 — Single shared lock (status quo)

One `requirements.txt` → one `requirements.lock` → one hub → one binary.

Implement: nothing new. Add a dep to `requirements.txt`, run
`pip-compile --allow-unsafe --output-file=requirements.lock requirements.txt`.

- **Pros:** simplest, fully hermetic (Bazel resolves everything), one cold
  start, smallest disk.
- **Cons:** cannot represent conflicting models; global blast radius.
- **Use when:** all models are mutually dep-compatible. Realistic for 2–3
  well-behaved models.

### Option 1 — Per-model lock + per-model pip hub (build-time isolation)

Each `ai/models/<m>/` owns a lock and a Bazel pip hub.

```python
# MODULE.bazel
pip.parse(
    hub_name = "joshua_pip_smolvla",
    python_version = "3.10",
    requirements_lock = "//ai/models/smolvla:requirements.lock",
)
use_repo(pip, "joshua_pip_smolvla")
```

```python
# ai/models/smolvla/BUILD
load("@joshua_pip_smolvla//:requirements.bzl", smolvla_req = "requirement")
py_library(name = "adapter", srcs = ["adapter.py"], deps = [
    smolvla_req("transformers"), smolvla_req("lerobot"),
    smolvla_req("pillow"), ...])
```

- **Pros:** each model's solve is independent and small; clean ownership.
- **Cons (critical):** does **not** fix runtime co-existence. If the one
  `inference` binary still depends on multiple model adapters, multiple hubs
  put *two* versions of e.g. `Pillow` into one runfiles/`sys.path`; whichever
  is first wins — fragile and non-deterministic.
- **Use when:** combined with Option 2 or 3 (which provide the process/runtime
  boundary). On its own it is necessary but not sufficient.

### Option 2 — One node binary per model (static process isolation)

Give each model its own `ros2_py_binary` that embeds the generic host + only
that model's adapter (deps from that model's hub). The launcher spawns the
right binary per config.

```python
# ai/models/smolvla/BUILD
ros2_py_binary(
    name = "smolvla_node",
    srcs = ["//ai/runtime:host_main.py"],
    main = "host_main.py",
    set_up_ament = True,
    deps = [":adapter", "//ai/runtime:host", "//ros2:node_runner_py"],
)
```

- **Pros:** real isolation (one closure per binary); fully hermetic; no
  runtime env management.
- **Cons:** **binary explosion** — every model is a new Bazel target, new
  hub, new wiring; build graph and CI grow per model. Does not scale to many
  models, and cannot bridge different Python versions.
- **Use when:** a handful of models that rarely changes.

### Option 3 — Dynamic re-exec into a per-model venv (RECOMMENDED dynamic path)

One generic, model-free launcher. At runtime it reads the config, resolves the
selected model's locked closure to a cached virtualenv, and `exec`s the host
inside it. No binary per model; models are *data* (adapter + lock + manifest).

**3a. Launcher / bootstrap** (runs in a minimal base interpreter):

```python
# ai/runtime/bootstrap.py
import os, sys
from ai.runtime.manifest import resolve_model         # reads manifests
from ai.runtime.envmgr import ensure_model_env

def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    cfg = load_config(argv)                            # light: proto only
    model = resolve_model(cfg)                         # -> ModelManifest
    if model.isolation == "REEXEC" and not _already_in_env(model):
        venv_py = ensure_model_env(model)              # build/cache venv
        os.environ["JOSHUA_MODEL_ENV"] = model.name    # re-entry guard
        os.execv(venv_py, [venv_py, "-m", "ai.runtime.host_main", *argv])
    # else: deps already active -> run the host directly
    from ai.runtime.host_main import main as host_main
    return host_main(argv)
```

**3b. Environment manager** (content-hash cached; uses `uv` for speed):

```python
# ai/runtime/envmgr.py
import hashlib, os, subprocess
from pathlib import Path

CACHE = Path(os.environ.get("JOSHUA_ENV_CACHE", "~/.cache/joshua/envs")).expanduser()

def ensure_model_env(model) -> str:
    lock = Path(model.requirements_lock)
    key = hashlib.sha256(lock.read_bytes()).hexdigest()[:16]
    venv = CACHE / f"{model.name}-{key}"
    py = venv / "bin" / "python"
    if not py.exists():
        venv.parent.mkdir(parents=True, exist_ok=True)
        # --system-site-packages so rclpy (system ROS) is importable.
        subprocess.check_call(["uv", "venv", "--python", model.python_version,
                               "--system-site-packages", str(venv)])
        subprocess.check_call(["uv", "pip", "sync", "--python", str(py), str(lock)])
    return str(py)
```

**3c. rclpy / system-ROS constraint.** The venv must layer on top of system
ROS so `rclpy`, `std_msgs`, etc. resolve. Two safe approaches:
- `uv venv --system-site-packages` (above), or
- keep `PYTHONPATH` (set by `source /opt/ros/humble/setup.bash`) and prepend
  the venv `site-packages` ahead of it so the model's `numpy`/`Pillow` win
  over system versions.
The venv `python_version` **must equal** the system ROS Python (3.10 Humble /
3.12 Jazzy), or the `rclpy` C-extension won't load.

**3d. Deployment / hermeticity.** Runtime `uv sync` assumes network at first
run — unacceptable on a robot at boot. Mitigate by **pre-building** the envs:
- a Bazel/CI step runs `ensure_model_env` for each manifest and bakes the
  cache into the image (or a `oci_image` layer);
- the cache key is the lock hash, so a baked env is reused with zero network.
Locks remain the source of truth, so prebuilt == reproducible.

**3e. Launcher integration.** Today `ros2/inference.py` calls
`node_runner.run_node(InferenceHost, ...)`. Under Option 3, `ros2/inference.py`
becomes the thin `bootstrap.main`, and a new `ai/runtime/host_main.py` is the
post-exec entry that constructs `InferenceHost`. The C++ `node_generator` that
spawns nodes is unchanged — it still launches one `inference` target.

- **Pros:** O(1) wiring per model (lock + manifest line); no binary
  explosion; conflicts become *impossible* (each model an isolated env);
  supports per-model versions of everything except the Python minor (see 3c).
- **Cons:** runtime env management; cold-start build (mitigated by prebuild);
  N envs cost disk; slightly harder cross-`exec` debugging; `uv` becomes a
  host dependency.
- **Use when:** ≥2 genuinely conflicting models on the same Python — the
  expected steady state for a multi-VLA platform.

### Option 4 — Subprocess worker / sidecar (cross-Python, polyglot)

Split the boundary: the generic **rclpy host** stays in the base (3.10)
process and owns ROS topics; the **adapter** runs as a worker subprocess in
its own venv (any Python, or even another language), talking to the host over
a small local IPC.

```python
# host side: spawn worker in its venv, speak a tiny framed protocol
proc = subprocess.Popen([venv_py, "-m", "ai.runtime.worker", model.name],
                        stdin=PIPE, stdout=PIPE)            # or UNIX socket / shared mem
# worker side: import adapter, loop: read Observation -> ActionCommand
```

Transport options: stdin/stdout framed protobuf, UNIX domain socket, or
shared memory for image tensors (avoid copying frames over a pipe). The
adapter contract is unchanged; only a `WorkerAdapter` shim that proxies calls
across the boundary is added.

- **Pros:** removes the Python-minor constraint; enables non-Python models;
  fault isolation (a crashing model doesn't kill the host).
- **Cons:** IPC serialization cost (especially images); more moving parts;
  another protocol to maintain.
- **Use when:** a model needs a different Python/runtime, or you want crash
  isolation.

### Option 5 — Container / microservice per model (production serving)

Each model is a long-running server in its own OCI image (its own lock baked
in), exposed over gRPC or a ROS bridge. The host calls it like any node. This
is the industry endgame (KServe / Triton / Ray Serve / TorchServe style).

```
ai/models/smolvla/Dockerfile  ->  smolvla-serve:<tag>  (uvicorn/gRPC + adapter)
host -> gRPC/ROS2 -> model container (independent deploy, GPU scheduling, scale)
```

- **Pros:** maximal isolation; independent deploy/scaling; GPU sharing; polyglot;
  matches cloud model-serving infra.
- **Cons:** heaviest ops (orchestration, image registry, network hop, larger
  disk/RAM); overkill for a robot running 1–2 models locally.
- **Use when:** server-side serving at scale or heterogeneous hardware.

## 6. Manifest schema (the declarative source of truth)

A per-model manifest ties everything together and is what the registry,
bootstrap, env manager, and prebuild step all read. Proposed proto:

```proto
// ai/runtime/proto/model_manifest.proto
syntax = "proto3";
package ai.runtime;

import "ai/proto/ai_model.proto";   // ModelType

enum Isolation {
  IN_PROCESS = 0;   // Option 0/1
  REEXEC     = 1;   // Option 3
  WORKER     = 2;   // Option 4
  CONTAINER  = 3;   // Option 5
}

message ModelManifest {
  string name = 1;                  // "smolvla"
  ai.ModelType model_type = 2;      // SMOLVLA
  string entrypoint = 3;            // "ai.models.smolvla.adapter:SmolVlaAdapter"
  string requirements_lock = 4;     // "ai/models/smolvla/requirements.lock"
  string python_version = 5;        // "3.10"
  Isolation isolation = 6;          // REEXEC
  string container_image = 7;       // for CONTAINER
}
```

Manifests live at `ai/models/<m>/model.textproto`. The registry is **generated**
from the set of manifests, so adding a model never edits shared `registry.py`.
Until codegen exists, the existing hand-written `ADAPTER_LOADERS` map is the
interim manifest.

### Adding a model after this lands

1. `ai/models/<m>/`: write `adapter.py`, `<m>_config.proto`, `requirements.in`,
   `model.textproto`.
2. `tools/lock_model.sh <m>` → generates `requirements.lock` for that model.
3. `bazel build //ai/models/<m>:adapter` (and prebuild its env in CI).

No edits to the global lock, no new binary, no risk to other models.

## 7. Comparison

| | Opt 0 shared | Opt 2 binary/model | Opt 3 re-exec venv | Opt 4 worker | Opt 5 container |
|---|---|---|---|---|---|
| Resolves version conflicts | ❌ | ✅ | ✅ | ✅ | ✅ |
| Different Python per model | ❌ | ❌ | ❌ | ✅ | ✅ |
| New-model wiring cost | trivial | high (target+hub) | low (lock+manifest) | low | medium |
| Build-time blast radius | global | per binary | per env | per env | per image |
| Hermetic / reproducible | ✅ | ✅ | ✅ (prebuilt) | ✅ (prebuilt) | ✅ |
| Cold start | fast | fast | build-once then fast | build-once | image pull |
| Disk footprint | 1× | N× (in build) | N× (cached) | N× | N× (images) |
| Ops complexity | lowest | low | medium | medium-high | highest |

## 8. Recommendation & phased rollout

The "best" architecture is **clean contract + manifest, with isolation as an
escalating strategy adopted only when forced** — not committing prematurely to
re-exec machinery.

- **Phase 0 (now):** Fix the SmolVLA bug at the dependency root and seed the
  per-model pattern (Section 9). Keep Option 0 for the live binary because we
  have exactly one conflict today and it is resolvable globally.
- **Phase 1:** Land the `ModelManifest` proto + generate the registry from it.
  Pure seam work; no behavior change.
- **Phase 2 (when a 2nd genuinely-conflicting model arrives):** Implement
  Option 3 — `bootstrap.py`, `envmgr.py`, `host_main.py`, `tools/lock_model.sh`,
  CI prebuild. Migrate per-model pins out of the global lock into per-model
  locks.
- **Phase 3 (if/when needed):** Option 4 for a cross-Python model; Option 5
  for server-side serving at scale.

## 9. Immediate fix (Phase 0)

Because the live `inference` binary is still single-process (Option 0), the
working fix today is to correct the **global** pins, and to record the SmolVLA
*tested triple* as a seed `requirements.in` for the per-model future:

1. `requirements.txt`: `Pillow==11.3.0` → `Pillow==11.2.1`;
   `num2words==0.5.13` → `num2words==0.5.14`.
2. Regenerate: `pip-compile --allow-unsafe --output-file=requirements.lock requirements.txt`
   (and the 3.12 lock if it carries these pins).
3. Seed `ai/models/smolvla/requirements.in` documenting the known-good triple
   (`lerobot==0.4.1`, `transformers==4.53.2`, `Pillow==11.2.1`,
   `num2words>=0.5.14`). When Option 3 lands, this file becomes the source for
   SmolVLA's isolated lock and the global pins are removed.

## 10. Risks & open questions

- **`uv` as a host dependency** (Option 3): acceptable? Alternative is
  `pip install -r` (slower) or Bazel-built venvs.
- **Disk on edge devices:** N torch envs may be too large; may force Option 5
  off-device or shared base layers.
- **Python-version drift:** Humble (3.10) vs Jazzy (3.12) already splits the
  repo into two locks; per-model envs must track the target platform's Python.
- **Registry codegen:** generate from manifests vs keep the hand-written map —
  decide in Phase 1.
- **Action contract stability:** unrelated to deps but worth versioning the
  `Observation`/`ActionCommand` contract before many models depend on it.
