# AI Pipeline RFC: Modular Inference, Training, and Simulation Integration

## Purpose

Define a modular, config-driven AI architecture for Joshua that supports:

- Universal inference runtime for multiple model families (VLA and non-VLA)
- Pluggable training algorithms (BC, PPO, future methods)
- Backend-agnostic simulation integration (MuJoCo, Isaac Sim, future MJX)

This RFC is designed to align with Joshua's current config-first runtime model while avoiding tight coupling between model logic, ROS runtime, and simulator details.

## Current Context

### What already works well

- One `.pbtxt` drives the full runtime.
- `joshua_main` is the unified launcher entrypoint.
- Simulation backends are now clearly separated under `simulation/mujoco` and `simulation/isaac`.
- Data collection exists and is production-usable via DataStore.

### Current pain points

- Inference node tries to be universal but model-specific mapping logic leaks into shared runtime.
- Training path was removed and needs a clean reintroduction.
- Simulation and training are not yet connected through a single stable API.
- Data pipeline records interleaved streams; training-ready synchronization is not yet first-class.

## Design Goals

1. **Modularity**: swap model backends, trainers, and simulators independently.
2. **Configurability**: behavior should come from config, not code branching.
3. **Compatibility**: support multiple model families without rewriting inference host.
4. **Isolation**: keep backend-specific dependencies inside adapters.
5. **Incremental migration**: preserve current working inference/simulation while adding training.

## Non-Goals

- Reintroducing the old RL pipeline architecture.
- Mixing Isaac runtime dependencies into Joshua's Bazel Python process.
- Building all trainer algorithms at once in phase 1.

## Target Architecture

## 1) AI layers

### `ai/core`

Framework-agnostic model logic.

- Owns: checkpoint loading, predict, train_step.
- Does not own: ROS topics/messages, simulator internals.

### `ai/runtime`

Inference runtime host and model adapters.

- Host owns: ROS pub/sub lifecycle, buffering, tick loop, health logs.
- Adapter owns: preprocess, infer, postprocess, model-specific schemas.

### `ai/data`

Data collection and dataset preparation.

- Owns: DataStore recording, schema conversion, sync/resample/export pipeline.

### `ai/training`

Training orchestration and algorithms.

- Owns: loop control, rollout, update, eval, checkpointing.
- Depends on: `ai/core`, `ai/data`, and `simulation/api`.

## 2) Simulation layers

### `simulation/api`

Backend-agnostic simulator contract.

Example API:

- `reset(seed) -> obs_dict`
- `step(action_dict) -> {obs, reward, terminated, truncated, info}`
- `spec() -> observation/action schema`
- `close()`

### Backend adapters

- `simulation/mujoco/*` implements the API in-process.
- `simulation/isaac/*` implements the API via subprocess worker/session IPC.
- Optional future `simulation/mjx/*` implements same API.

## 3) Contract layer (required for universality)

A universal architecture only works if obs/action contracts are explicit.

- Canonical logical names (`joint_pos`, `images.front`, `gripper_cmd`, etc.).
- Mapping from logical names to backend-specific joints/sensors/topics.
- Startup validation that fails fast on missing/mismatched signals.

## Joshua Integration Plan

## Launcher integration

- Keep `MODE_SIMULATION` path as-is for visualization workflows.
- Reintroduce a dedicated `MODE_TRAINING` path in launcher for training orchestration (`RunTraining()`).
- Training should not be implicit under inference or simulation modes.

## Config integration

Keep concerns separated:

- `simulation { ... }`: assets, backend selection, viewer/mirror/offscreen behavior.
- new `training { ... }`: algorithm, task config, rollout/hparams, backend override if needed.

Do not overload viewer mode fields with training semantics.

## Inference integration

- Keep one `INFERENCE` node type.
- Convert current inference to host+adapter design.
- Register model adapters by model type/provider.

## Data integration

- Keep DataStore behavior stable.
- Add training-oriented dataset transforms (sync/resample/windowing) under `ai/data`.

## Execution Flows

## A) Inference (online)

1. Load config.
2. Inference host resolves model adapter/provider.
3. Host receives ROS observations and executes fixed-rate inference tick.
4. Adapter converts obs to model input and model output to canonical actions.
5. Host publishes actuator commands.

## B) Training (offline or sim-in-the-loop)

1. Launcher dispatches training mode.
2. Training runner creates simulator via `simulation/api` factory.
3. Task env applies reward/done/reset logic over simulator transitions.
4. Trainer algorithm collects rollouts and updates policy.
5. Save checkpoints, metrics, and optional rollout artifacts.

## C) Training with simulation backend swap

Same trainer and task code:

- MuJoCo adapter in-process for first MVP.
- Isaac adapter via subprocess IPC for parity.
- Future MJX adapter with same contract.

Only config and adapter selection change.

## Risks and Mitigations

- **Risk: adapter divergence between backends**
  - Mitigation: contract tests for mapping/spec parity.
- **Risk: Isaac IPC overhead**
  - Mitigation: batched step protocol and coarse-grained message payloads.
- **Risk: growing inference host complexity**
  - Mitigation: keep host logic minimal and move model specifics to adapters.
- **Risk: dataset mismatch for training**
  - Mitigation: make sync/resample pipeline first-class in `ai/data`.

## Acceptance Criteria

1. Adding a new model family requires only:
   - new adapter + provider registration
   - no edits to inference host core flow
2. Swapping MuJoCo -> Isaac in training requires config change only.
3. Training algorithms run against `simulation/api` without backend imports.
4. Config validation catches schema/mapping mismatch before runtime loop.
5. Existing simulation viewer workflows remain unchanged.

## Recommended Rollout

1. Define interfaces and config schemas (no behavior change).
2. Refactor inference host+adapter with current models.
3. Implement training MVP with MuJoCo adapter.
4. Add Isaac step-capable adapter session.
5. Expand algorithms and data transforms.
