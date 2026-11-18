# AI and data pipelines

Follow the repository [AGENTS.md](../AGENTS.md) and
[coding guidelines](../docs/CODING_GUIDELINES.md). Read the local
[README.md](README.md) before editing. This file owns the rules and architecture
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
