# Training RFC: Modular Imitation Learning + Simulation RL

Status: Draft (design only — no implementation in this document)
Companion to: [../ai/README.md](../ai/README.md) (current inference stack)

## 1. Purpose

Define a modular, config-driven **training** architecture for Joshua that
supports two tracks behind one consistent framework:

- **Track A — Imitation Learning (IL):** collect teleoperation
  demonstrations with `DataStore`, then supervised fine-tune a policy
  (e.g. SmolVLA, ACT) on that data.
- **Track B — Reinforcement Learning (RL):** train a policy from reward in
  simulation (MuJoCo / Isaac) with sim-to-real in mind.

The design mirrors the inference refactor we already shipped: a
**model-agnostic engine** plus **per-model plug-ins**, driven by protobuf
config. It deliberately reconciles with the *as-built* code, which differs
from an early layered sketch (there is no `ai/core`; model
logic lives in `ai/models/<model>/`).

For the conceptual background (how IL/RL training works in industry and
academia, the universal training loop, and evaluation), this RFC assumes
that framing and focuses on Joshua's architecture.

## 2. Scope

In scope:

- Engine/contract/plug-in decomposition for training.
- `TrainingConfig` proto schema.
- The dataset synchronization layer (interleaved stream → training-ready).
- The backend-agnostic simulation environment API (for RL).
- Execution flows, phasing, risks, acceptance criteria.

Out of scope (future RFCs):

- Distributed/multi-node training (FSDP), hyperparameter search.
- A model registry / experiment-tracking service beyond a logging hook.
- Cloud data flywheel automation.

## 3. Where Joshua is today

| Capability | Status | Location |
| --- | --- | --- |
| Data collection (record ROS topics) | ✅ working | `ai/train/data_store.py` (`DataStore`) |
| Dataset inspection | ✅ working | `ai/train/data_load.py` |
| Inference engine (model-agnostic host) | ✅ working | `ai/inference/` |
| Per-model inference plug-ins | ✅ working | `ai/models/<model>/adapter.py` |
| Model catalog enum | ✅ working | `ai/proto/ai_model.proto` (`ModelType`) |
| Simulation backends (viewer) | ✅ working | `simulation/mujoco/`, `simulation/isaac/` |
| **Dataset sync → (obs, action) pairs** | ❌ missing | — |
| **Training engine + algorithms** | ❌ missing | — |
| **Per-model training behavior** | ❌ missing | — |
| **Backend-agnostic sim env API (step/reward)** | ❌ missing | simulation backends are viewer-only |

Two structural facts shape the design:

1. **The model catalog is shared.** `ModelType` (SmolVLA, …) already
   identifies a model for inference; training reuses the same enum so a
   model is trained and served by the same identity.
2. **`DataStore` output is an interleaved message stream**, not
   `(observation, action)` pairs. Per `ai/train/README.md`, this must be
   synchronized/resampled before IL training. This is the foundational
   missing piece.

## 4. Design goals

1. **Symmetry with inference.** Same mental model: engine + per-model
   plug-ins + registry + proto config.
2. **Two tracks, one framework.** IL and RL share cross-cutting services
   (optimizer, checkpoint, logging, eval) and differ only in their data
   source and update rule.
3. **Backend independence for RL.** Algorithms never import MuJoCo/Isaac;
   they talk to a `simulation/api` contract.
4. **Dependency isolation.** Heavy training deps stay in the model/algo
   plug-in `BUILD` targets, exactly like the SmolVLA inference adapter.
5. **Offline-job friendly.** Training is a batch job (`bazel run`), not a
   ROS node, though it may be dispatched via the launcher for consistency.
6. **Incremental.** Land the dataset layer and an IL MVP before RL.

## 5. The segmentation (recap)

All training decomposes into orthogonal blocks. The framework provides the
seams; plug-ins fill them:

```
                 ┌──────────────────────── Trainer engine ───────────────────────┐
 Data source ──▶ │ loop · optimizer · scheduler · AMP · checkpoint · log · eval   │ ──▶ checkpoint
                 └───────────────────────────────────────────────────────────────┘
        ▲                                   ▲                              ▲
        │                                   │                              │
   IL: Dataset                         Policy (per model)            Algorithm (update rule)
   RL: Sim env + Task                  (SmolVLA / ACT / actor-critic) (BC / PPO / ...)
```

| Block | IL (Track A) | RL (Track B) | Owner |
| --- | --- | --- | --- |
| Data source | `LeRobotDataset` from demos | `Environment` (sim) + `Task` (reward/done) | `ai/data` + `simulation/api` |
| Policy | model with `compute_loss(batch)` | actor-critic with `act` / `evaluate_actions` | `ai/models/<model>/` |
| Algorithm | supervised loop (BC) | rollout loop (PPO/SAC) | `ai/training/algorithms/` |
| Engine services | shared | shared | `ai/training/` |

## 6. Target architecture

### 6.1 Directory layout

```text
ai/
  inference/                  # inference engine (exists, unchanged)
  models/                     # per-model plug-ins (exists)
    smolvla/
      smolvla_config.proto
      adapter.py              # inference behavior (exists)
      policy.py               # NEW: training behavior (SupervisedPolicy / RLPolicy)
      BUILD
    random_noise/ ...
    registry.py               # inference adapters (exists)
    training_registry.py      # NEW: ModelType -> training policy
  data/                       # data collection + dataset prep
    data_store.py             # recording (moved from ai/train)
    data_load.py              # inspection (moved from ai/train)
    datasets/                 # NEW: sync/resample/export → LeRobotDataset, dataloaders
    BUILD
  training/                   # NEW: model-agnostic training engine
    proto/training.proto      # TrainingConfig + enums + hparams
    trainer.py                # base Trainer: loop scaffolding, ckpt, log, eval
    contracts.py              # Policy / Algorithm / DatasetProvider / Task interfaces
    registry.py               # Algorithm enum -> trainer
    algorithms/
      bc.py                   # supervised IL loop
      ppo.py                  # RL loop
    train_main.py             # py_binary entrypoint (bazel run //ai/training:train)
    BUILD
  proto/
    ai_model.proto            # ModelType (shared inference + training)

simulation/
  api/                        # NEW: backend-agnostic env contract
    env.py                    # Environment ABC: reset/step/spec/close
    BUILD
  mujoco/                     # implements simulation/api (in-process)  [extend]
  isaac/                      # implements simulation/api (subprocess IPC) [extend]
```

> **Naming note / decision:** `ai/train` today only holds *data* tooling,
> which is misleading. This RFC recommends renaming `ai/train` → `ai/data`
> and introducing `ai/training` for the engine. (Alternative: keep
> `ai/train` and add `ai/training`; the names will be confusingly close.)

### 6.2 Contracts (interfaces)

These are illustrative signatures, not final code.

**Policy (model-side, in `ai/models/<model>/policy.py`):** two small
interfaces so a model can support either or both tracks.

```python
class SupervisedPolicy(ABC):           # Track A
    def build(self) -> None: ...                       # construct / load pretrained
    def compute_loss(self, batch) -> tuple[Tensor, dict]: ...  # loss, metrics
    def predict(self, obs) -> dict: ...                # for offline eval
    def state_dict(self) -> dict: ...
    def load_state_dict(self, sd) -> None: ...

class RLPolicy(ABC):                   # Track B
    def build(self) -> None: ...
    def act(self, obs) -> ActorOutput: ...             # action, value, log_prob
    def evaluate_actions(self, obs, act) -> EvalOutput: ...  # log_prob, value, entropy
    def state_dict(self) -> dict: ...
    def load_state_dict(self, sd) -> None: ...
```

**Algorithm (engine-side, in `ai/training/algorithms/`):**

```python
class Trainer(ABC):                    # base engine services
    # owns: device, AMP, optimizer/scheduler build, checkpointing,
    #       metric logging, eval cadence, resume.
    def fit(self) -> None: ...

class BCTrainer(Trainer):              # Track A: needs DatasetProvider + SupervisedPolicy
    ...

class PPOTrainer(Trainer):            # Track B: needs Environment + Task + RLPolicy
    ...
```

**Data source:**

```python
class DatasetProvider(ABC):            # Track A
    def dataloader(self) -> Iterable[Batch]: ...       # batched, model-formatted
    def spec(self) -> DataSpec: ...                    # obs/action keys, shapes

class Task(ABC):                       # Track B (reward/done over an env)
    def reset_state(self) -> None: ...
    def observation(self, raw) -> dict: ...
    def reward(self, raw, action) -> float: ...
    def done(self, raw) -> tuple[bool, bool]: ...      # terminated, truncated
```

**Simulation environment (`simulation/api/env.py`, RL only):**

```python
class Environment(ABC):
    def reset(self, seed: int | None = None) -> dict: ...        # obs
    def step(self, action: dict) -> StepResult: ...             # obs, reward, term, trunc, info
    def spec(self) -> EnvSpec: ...                              # obs/action schema
    def close(self) -> None: ...
```

`simulation/mujoco` implements this in-process; `simulation/isaac`
implements it via the existing subprocess/IPC pattern (Isaac stays out of
the Bazel Python process).

### 6.3 Canonical obs/action contract

Universality requires explicit signal names. Both datasets and sim envs map to canonical logical
names: `observation.images.<cam>`, `observation.state`,
`action.joint_pos`, `action.gripper`, … Startup validation fails fast on
missing/mismatched signals so a config error never reaches the train loop.

## 7. Config schema (`ai/training/proto/training.proto`)

Sketch only. Reuses `ai.models.ModelType`.

```protobuf
enum TrainerAlgorithm {
  TRAINER_INVALID = 0;
  BC = 1;            // supervised imitation
  PPO = 2;           // on-policy RL
  // SAC, DIFFUSION, ... later
}

message OptimizerConfig { double lr = 1; double weight_decay = 2; }
message SchedulerConfig { string type = 1; uint32 warmup_steps = 2; }
message CheckpointConfig { uint32 every_steps = 1; uint32 keep_last = 2; string best_metric = 3; }
message LoggingConfig { string backend = 1; string project = 2; uint32 every_steps = 3; }
message EvalConfig { uint32 every_steps = 1; bool in_sim = 2; uint32 num_episodes = 3; }

message ImitationConfig {                 // Track A
  string dataset_path = 1;                // synced LeRobotDataset
  uint32 batch_size = 2;
  uint32 num_workers = 3;
  uint32 action_chunk = 4;                // ACT-style chunking
  repeated string observation_keys = 5;
  repeated string action_keys = 6;
}

message RLConfig {                         // Track B
  simulation.SimulatorBackend backend = 1; // reuse existing enum
  string task = 2;                          // task id (reward/done)
  uint32 num_envs = 3;
  uint32 rollout_steps = 4;
  double gamma = 5;
  double gae_lambda = 6;
  double clip_range = 7;
  bool domain_randomization = 8;
}

message TrainingConfig {
  TrainerAlgorithm algorithm = 1;
  ai.models.ModelType model_type = 2;       // shared catalog with inference
  string output_dir = 3;
  uint32 max_steps = 4;
  uint32 seed = 5;
  string device = 6;                        // "cuda" | "cpu"
  bool mixed_precision = 7;

  OptimizerConfig optimizer = 8;
  SchedulerConfig scheduler = 9;
  CheckpointConfig checkpoint = 10;
  LoggingConfig logging = 11;
  EvalConfig eval = 12;

  oneof source {
    ImitationConfig imitation = 13;
    RLConfig rl = 14;
  }
}
```

Keep `training { ... }` separate from
`simulation { ... }`; do not overload viewer fields with training
semantics.

## 8. Execution flows

### Track A — Imitation learning (offline)

```
bazel run //ai/training:train -- --config=.../so100_bc.pbtxt

1. Load TrainingConfig.
2. training_registry resolves the model's SupervisedPolicy by model_type.
3. registry resolves BCTrainer by algorithm = BC.
4. DatasetProvider opens the synced LeRobotDataset → DataLoader.
5. BCTrainer.fit():
     for step: batch -> policy.compute_loss -> backward -> step
               log metrics; periodic offline eval (action error);
               optional in-sim rollout eval; checkpoint best/last.
6. Output: checkpoint consumable by the SmolVLA inference adapter.
```

### Track B — RL in simulation (sim-in-the-loop)

```
bazel run //ai/training:train -- --config=.../ant_ppo.pbtxt

1. Load TrainingConfig (source = rl).
2. simulation/api factory builds Environment for RLConfig.backend
   (MuJoCo in-process, or Isaac via subprocess IPC).
3. Task wraps the env with reward/done/reset.
4. training_registry resolves the model's RLPolicy; registry resolves PPOTrainer.
5. PPOTrainer.fit():
     collect rollouts (env.step) -> compute advantages (GAE)
       -> PPO update on RLPolicy -> log -> eval success rate -> checkpoint.
6. Output: checkpoint + optional rollout artifacts.
```

### Track B backend swap

Identical trainer/task/policy code; only `RLConfig.backend` and the
adapter selection change (MuJoCo → Isaac → future MJX).

## 9. The dataset synchronization layer (foundational)

This unblocks all of Track A and is phase 1.

- **Input:** `DataStore` interleaved stream (rows = single ROS message
  events with `topic`, `timestamp`, `episode_index`, `image`/`data`).
- **Process:** per episode, **time-align** heterogeneous topics onto a
  common timeline (resample/interpolate to a target rate), assemble
  `(observation, action)` frames, normalize, and **export to
  `LeRobotDataset`** layout (so the existing LeRobot/SmolVLA stack
  consumes it directly).
- **Output:** a versioned, training-ready dataset directory.
- **Home:** `ai/data/datasets/` with a small CLI
  (`bazel run //ai/data:sync -- --in=... --out=... --rate_hz=...`).

Design choices to settle: resampling policy (nearest vs. interpolate),
action definition (absolute next state vs. delta vs. recorded command),
chunking window, and train/val split strategy.

## 10. Launcher & build integration

- **Primary path:** training is an offline `py_binary`
  (`//ai/training:train`), run directly. This matches how IL jobs run in
  practice and keeps training off the ROS critical path.
- **Optional launcher path:** reintroduce `MODE_TRAINING` →
  `RunTraining()` in `joshua_main` for a single consistent entrypoint.
  Training must not be implicit under inference/simulation
  modes.
- **Dependency isolation:** model/algorithm heavy deps (torch, lerobot,
  RL libs) live only in the relevant `BUILD` targets; the engine core
  stays light. Lazy registries (as in inference) keep imports on-demand.

## 11. Phased rollout

| Phase | Deliverable | Unblocks |
| --- | --- | --- |
| **0** | Interfaces + `training.proto` + directory scaffolding (no behavior) | everything |
| **1** | **Dataset sync layer** (interleaved → `LeRobotDataset`) + CLI + inspection | Track A |
| **2** | IL MVP: `BCTrainer` + SmolVLA `SupervisedPolicy` + checkpoint/log/offline-eval | first trained model |
| **3** | `simulation/api` env contract + MuJoCo in-process implementation | Track B |
| **4** | RL MVP: `PPOTrainer` + actor-critic `RLPolicy` + in-sim eval on MuJoCo | first RL policy |
| **5** | Isaac `simulation/api` via subprocess IPC (backend parity) | Isaac RL |
| **6** | More algorithms/models (ACT, diffusion, SAC), data transforms, richer eval | breadth |

Phases 1–2 (IL) and 3–4 (RL) are independent after phase 0 and can proceed
in parallel.

## 12. Risks & mitigations

- **Dataset semantics wrong (bad action labels / desync).** → Make the
  sync layer first-class with inspection tooling and explicit
  action-definition config; validate before training.
- **Sim-to-real gap (Track B).** → Domain randomization knobs in
  `RLConfig`; treat in-sim success as necessary-not-sufficient.
- **Backend adapter divergence (MuJoCo vs Isaac).** → Contract tests for
  `simulation/api` spec/step parity.
- **Engine bloat.** → Keep `Trainer` services generic; push model/algo
  specifics into plug-ins, mirroring the inference host discipline.
- **Heavy deps leaking into core.** → Per-plug-in `BUILD` targets + lazy
  registries.

## 13. Acceptance criteria

1. Adding a trainable model requires only a `policy.py` in its
   `ai/models/<model>/` dir + one `training_registry` entry — no engine
   edits.
2. Adding an algorithm requires only a new `algorithms/<algo>.py` + one
   registry entry.
3. Swapping MuJoCo → Isaac for RL is a config change only; algorithms
   import no backend.
4. A SmolVLA checkpoint produced by Track A loads directly in the existing
   `SmolVlaAdapter` inference path.
5. Config validation catches schema/signal mismatch before the train loop.
6. Existing inference and simulation-viewer workflows remain unchanged.

## 14. Open questions

1. **Naming:** rename `ai/train` → `ai/data` (recommended) vs. keep it?
2. **Action definition** for IL labels: recorded command vs. next observed
   state vs. delta?
3. **Experiment tracking** backend: W&B vs. TensorBoard vs. both behind
   the `LoggingConfig` hook?
4. **Where RL policies live:** co-located in `ai/models/<model>/` (keeps
   one-model-one-dir) vs. a separate `ai/training/policies/`?
5. **Launcher:** is `MODE_TRAINING` worth reintroducing, or is the
   standalone `py_binary` sufficient?
```
