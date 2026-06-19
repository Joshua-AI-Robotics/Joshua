# AI Pipeline Phase 1 Task List (Joshua File-Mapped Plan)

This is an implementation checklist for Phase 1: interfaces, inference modularization, and training MVP on MuJoCo.

## Scope

- Build stable contracts first.
- Preserve existing working inference/simulation behavior.
- Deliver one end-to-end training loop with MuJoCo.

## Out of Scope (Phase 1)

- Full Isaac training parity (phase 2+).
- Multiple trainer algorithms beyond one MVP.
- Large-scale distributed training.

## Workstream A: Contracts and Config

## A1. Add training config schema

**Files**
- `config/proto/config.proto`
- `config/proto/BUILD`
- `ai/proto/BUILD`
- new `ai/proto/training.proto`

**Tasks**
- Add `MODE_TRAINING` in `General.OperationMode`.
- Add `training` field in top-level `Config`.
- Create `TrainingConfig` message with:
  - algorithm id
  - task id
  - rollout params
  - checkpoint/output paths
  - optional simulator backend override

**Done when**
- Config parses for training presets.
- Existing non-training presets unaffected.

## A2. Define simulator API contract

**Files**
- new `simulation/api/simulator.py`
- new `simulation/api/types.py`
- new `simulation/api/factory.py`
- new `simulation/api/__init__.py`
- `simulation/BUILD` (targets/deps)

**Tasks**
- Define abstract simulator methods (`reset`, `step`, `spec`, `close`).
- Define canonical observation/action specs and step result type.
- Add backend factory for adapter selection.

**Done when**
- `simulation/api` has unit-testable interfaces.
- No backend-specific imports in API module.

## A3. Define inference adapter contract

**Files**
- new `ai/inference/adapter.py`
- new `ai/inference/types.py`
- new `ai/inference/registry.py`
- new `ai/inference/__init__.py`

**Tasks**
- Define `InferenceAdapter` interface:
  - `spec()`
  - `preprocess()`
  - `infer()`
  - `postprocess()`
- Define canonical runtime observation/action payload types.

**Done when**
- Adapters can be instantiated by registry.
- Schema contract available for startup validation.

## Workstream B: Inference Node Modularization

## B1. Split host vs model-specific logic

**Files**
- `ros2/inference.py`
- `ai/models/model_registry.py` (or bridge registry temporarily)
- `ai/models/smolvla/smolvla.py`
- `ai/models/random_noise/random_noise.py`
- new `ai/inference/host.py`

**Tasks**
- Keep `ros2/inference.py` as host shell (node lifecycle, buffering, publish).
- Move model-specific preprocess/postprocess into adapters.
- Keep current topic behavior and output semantics.

**Done when**
- Existing SmolVLA and RandomNoise presets still run.
- Host file no longer contains model-family specific transforms.

## B2. Add startup schema checks

**Files**
- `ai/inference/host.py`
- `ros2/inference.py`

**Tasks**
- Validate that required adapter inputs exist in config/topic mapping.
- Validate output channels before node spin.
- Fail fast with clear actionable errors.

**Done when**
- Misconfigured preset errors are deterministic and readable.

## Workstream C: MuJoCo Training MVP

## C1. Create MuJoCo simulator adapter

**Files**
- `simulation/mujoco/engine.py` (refactor or wrap)
- new `simulation/mujoco/adapter.py`
- `simulation/mujoco/BUILD`

**Tasks**
- Implement `Simulator` contract via MuJoCo backend.
- Keep existing mode scripts intact for viewer workflows.
- Ensure adapter is headless-compatible for training loops.

**Done when**
- Training env can step/reset through adapter without mode modules.

## C2. Create training environment wrapper

**Files**
- new `ai/training/env.py`
- new `ai/training/tasks/base_task.py`
- new `ai/training/tasks/<mvp_task>.py`
- new `ai/training/BUILD`

**Tasks**
- Build env wrapper over simulator adapter.
- Implement one task: obs projection + reward + done/reset logic.
- Return transitions in a trainer-consumable format.

**Done when**
- Rollout loop can run fixed number of steps and produce transitions.

## C3. Implement one trainer backend

**Files**
- new `ai/training/trainers/base_trainer.py`
- new `ai/training/trainers/<mvp_trainer>.py`
- new `ai/training/runner.py`

**Tasks**
- Implement one algorithm (BC or PPO) as MVP.
- Add checkpoint save/load and metrics logging.
- Keep algorithm isolated from simulator imports.

**Done when**
- End-to-end training run completes from a training preset.

## Workstream D: Launcher and Presets

## D1. Add training launcher dispatch

**Files**
- `launcher/joshua_main.cc`
- new `launcher/training_launcher.cc`
- new `launcher/training_launcher.h`
- `launcher/BUILD`

**Tasks**
- Add `MODE_TRAINING` branch calling `RunTraining()`.
- Launch training runner as subprocess with config path.

**Done when**
- `bazel run //launcher:joshua_main -- --config <train_preset>` enters training path.

## D2. Add training presets

**Files**
- new `config/config_preset/<robot>/*_train_*.pbtxt`
- `config/config_preset/BUILD` (if needed)

**Tasks**
- Add at least one MuJoCo training preset.
- Include simulation backend + training config sections.

**Done when**
- Preset parses and launches training end-to-end.

## Workstream E: Data and Documentation

## E1. Data pipeline handoff

**Files**
- `ai/train/data_store.py` (or migrated `ai/data/data_store.py`)
- new `ai/data/sync.py`
- new `ai/data/export.py`

**Tasks**
- Define conversion from interleaved records to training windows/batches.
- Keep backward compatibility for existing DataStore outputs.

**Done when**
- Training runner can consume generated datasets directly.

## E2. Docs and examples

**Files**
- `docs/AI_PIPELINE_RFC.md`
- `docs/GETTING_STARTED.md`
- `docs/ARCHITECTURE.md`
- `ai/train/README.md` (or `ai/data/README.md`)

**Tasks**
- Document inference host+adapter architecture.
- Document training config and run commands.
- Add migration notes from old RL pipeline.

**Done when**
- New contributor can run one inference and one training flow by docs only.

## Validation Checklist

- `bazel build //...`
- `bazel test //...`
- Inference smoke tests:
  - existing SmolVLA preset
  - existing RandomNoise preset
- Training smoke test:
  - one MuJoCo training preset for fixed small step count
- Config validation:
  - mismatched mapping should fail startup clearly

## Suggested Task Order

1. A1 -> A2 -> A3 (contracts first)
2. B1 -> B2 (inference modularization without behavior changes)
3. C1 -> C2 -> C3 (training MVP on MuJoCo)
4. D1 -> D2 (launcher + presets)
5. E1 -> E2 (data integration + docs)

## Phase 1 Exit Criteria

- Universal inference host running through adapter plugins.
- One training algorithm works end-to-end on MuJoCo via simulator API.
- Launcher supports explicit training mode.
- Training config is first-class in proto and presets.
- Docs are sufficient for another engineer to reproduce the flow.
