# MJX Environments — Term-Based Architecture

## Overview

MJX envs use a **term-based** architecture inspired by Isaac Lab's
`ManagerBasedRLEnvCfg`.  Instead of writing a monolithic `step()` with
reward / observation / termination logic baked in, each piece of the MDP
is a small, reusable **term function**.  A generic `MJXEnv` class
composes them automatically.

```
ai/train/mjx_envs/
├── __init__.py          # load_env() — dynamic task loader
├── base_env.py          # EnvState, StepResult (shared data types)
├── env.py               # MJXEnvCfg + MJXEnv (generic, term-based)
├── terms/               # Reusable term function libraries
│   ├── rewards.py       #   reward term factories
│   ├── observations.py  #   observation term factories
│   ├── terminations.py  #   termination term factories
│   └── resets.py        #   reset function factories
├── ant.py               # Ant locomotion config
├── reach.py             # Reach-to-target config
└── pick_place.py        # Pick-and-place config
```

---

## Core Concepts

### Term Functions

A term function is a **factory** that captures static config (body IDs,
thresholds, timestep) at init time and returns a pure-JAX callable for
use at step time.

There are four kinds:

| Kind          | Returned callable signature                    | Composed by `MJXEnv` via |
|---------------|------------------------------------------------|--------------------------|
| Reward        | `(data, state, action) -> scalar`              | Weighted sum             |
| Observation   | `(data, state) -> 1-D array`                   | Concatenation            |
| Termination   | `(data, state) -> bool scalar`                 | Logical OR               |
| Reset         | `(mjx_model, init_data, rng) -> (data, target)`| Called on done           |

**Arguments at step time:**

- `data` (`mjx.Data`) — physics state **after** stepping
- `state` (`EnvState`) — env state **before** stepping (contains
  `state.mjx_data` for velocity computation, `state.target_pos`, etc.)
- `action` (`jax.Array`) — the clipped action applied this step

### MJXEnvCfg

A dataclass that declares the full MDP:

```python
@dataclass
class MJXEnvCfg:
    frame_skip: int = 10
    max_episode_steps: int = 500

    rewards: List[RewardTerm]            # weighted sum  -> scalar reward
    observations: List[ObservationFn]    # concatenated  -> obs vector
    terminations: List[TerminationFn]    # OR'd          -> done flag
    reset_fn: ResetFn                    # auto-reset    -> new (data, target_pos)
```

### MJXEnv

A single generic class that interprets any `MJXEnvCfg`.  Its `step()`
and `reset()` are fully generic — task-specific behavior comes entirely
from the config.  Python for-loops over term lists unroll at JAX trace
time, so there is no performance overhead vs. hardcoded logic.

---

## How step() Works

```
action ──► clip ──► physics_step (frame_skip sub-steps)
                         │
                         ▼
               ┌─────────────────────┐
               │  Post-step mjx.Data │
               └────┬────┬────┬──────┘
                    │    │    │
          ┌─────────┘    │    └──────────┐
          ▼              ▼               ▼
    ┌──────────┐  ┌────────────┐  ┌──────────────┐
    │ Rewards  │  │ Obs terms  │  │ Terminations │
    │ (sum)    │  │ (concat)   │  │ (OR)         │
    └──────────┘  └────────────┘  └──────┬───────┘
          │              │               │
          │              │        terminated | truncated
          │              │               │
          ▼              ▼               ▼
       reward           obs         done ──► auto-reset
```

**Reward:** Each `RewardTerm.func(data, state, action)` returns a
scalar.  The final reward is
`sum(term.weight * term.func(...) for term in cfg.rewards)`.

**Observation:** Each `ObservationFn(data, state)` returns a 1-D array.
The final obs is `jnp.concatenate([fn(data, state) for fn in cfg.observations])`.

**Termination:** Each `TerminationFn(data, state)` returns a boolean
scalar.  The final terminated flag is the logical OR of all terms.
Truncation (time-out) is always added automatically by `MJXEnv`.

**Auto-reset:** When `done = terminated | truncated`, `MJXEnv` calls
`cfg.reset_fn(mjx_model, init_data, rng)` and blends the reset state
with the stepped state using `jnp.where(done, ...)`.

---

## Adding a New Task

1. **Write any new term functions** in `terms/rewards.py`,
   `terms/observations.py`, etc. (skip if existing terms suffice).

2. **Create a task config file** (e.g. `humanoid.py`):

```python
# ai/train/mjx_envs/humanoid.py
import mujoco
from ai.train.mjx_envs.env import MJXEnv, MJXEnvCfg, RewardTerm
from ai.train.mjx_envs.terms import observations, resets, rewards, terminations

def Env(model_path: str, frame_skip: int = 5, **kwargs) -> MJXEnv:
    mj_model = mujoco.MjModel.from_xml_path(model_path)
    dt = mj_model.opt.timestep * frame_skip

    cfg = MJXEnvCfg(
        frame_skip=frame_skip,
        max_episode_steps=1000,
        rewards=[
            RewardTerm(rewards.forward_velocity(dt), weight=1.25),
            RewardTerm(rewards.healthy_bonus(z_low=1.0, z_high=2.0), weight=5.0),
            RewardTerm(rewards.action_l2(), weight=-0.1),
        ],
        observations=[
            observations.qpos(skip=2),
            observations.qvel(),
        ],
        terminations=[
            terminations.height_out_of_range(z_low=1.0, z_high=2.0),
        ],
        reset_fn=resets.locomotion_reset(initial_height=1.4),
    )
    return MJXEnv(cfg, mj_model)
```

3. **Add a config preset** (e.g. `config/config_preset/humanoid_train_mjx.pbtxt`):

```protobuf
general {
  operation_mode: MODE_TRAINING
  name: "humanoid"
}
ai {
  training {
    environment: TRAINING_ENV_SIMULATION
    method: TRAINING_METHOD_RL
    model_path: "simulation/models/humanoid_mjx.xml"
    simulator_backend: SIM_BACKEND_MJX
    rl {
      task: "humanoid"
      frame_skip: 5
      total_timesteps: 50000000
      num_envs: 2048
      save_path: "humanoid_mjx_ppo"
    }
  }
}
```

4. **Run:**

```bash
bazel run //ai/train:trainer -- \
    --config config/config_preset/humanoid_train_mjx.pbtxt
```

No changes to `MJXEnv`, `mjx_rl.py`, or `trainer.py` required.

---

## Writing a Term Function

All term functions follow the **factory pattern**: a Python function that
captures static config and returns a pure-JAX closure.

### Reward Term

```python
# terms/rewards.py
def my_custom_reward(body_id: int, scale: float = 1.0) -> RewardFn:
    """Example: reward based on a body's height."""
    def fn(data: mjx.Data, state: EnvState, action: jax.Array) -> jax.Array:
        return scale * data.xpos[body_id, 2]
    return fn
```

Use it with a weight in the config:

```python
RewardTerm(rewards.my_custom_reward(body_id=3, scale=2.0), weight=0.5)
# Contribution to total reward: 0.5 * (2.0 * body_height)
```

### Observation Term

```python
# terms/observations.py
def body_velocity(body_id: int) -> ObservationFn:
    """Linear velocity of a body (6-D: 3 linear + 3 angular)."""
    def fn(data: mjx.Data, state: EnvState) -> jax.Array:
        return data.cvel[body_id]
    return fn
```

### Termination Term

```python
# terms/terminations.py
def joint_limit_exceeded(joint_ids: list, margin: float = 0.0) -> TerminationFn:
    """Terminate if any joint exceeds its limit."""
    ids = jnp.array(joint_ids)
    def fn(data: mjx.Data, state: EnvState) -> jax.Array:
        pos = data.qpos[ids]
        # ... check against limits ...
        return jnp.any(exceeded)
    return fn
```

### Reset Function

```python
# terms/resets.py
def my_reset(noise: float = 0.1) -> ResetFn:
    def fn(mjx_model, init_data, rng):
        # Randomize initial state
        data = ...
        target_pos = jnp.zeros(3)  # or random target
        return data, target_pos
    return fn
```

---

## Isaac Lab Integration Path

This architecture is designed for future multi-backend support.  The
config preset already specifies `simulator_backend`:

```
simulator_backend: SIM_BACKEND_MJX        # current
simulator_backend: SIM_BACKEND_ISAAC_SIM  # future
```

`trainer.py` dispatches to the right backend.  To add Isaac Lab:

1. Create `ai/train/isaac_envs/` mirroring this package structure.
2. Isaac Lab term implementations use PyTorch and wrap Isaac Lab's
   `mdp.*` functions.
3. The MDP structure (which terms, what weights) is portable — only the
   term implementations differ between JAX (MJX) and PyTorch (Isaac Lab).

```
trainer.py
  ├── SIM_BACKEND_MJX       → mjx_rl.py    → mjx_envs/{task}.py    → MJXEnv
  └── SIM_BACKEND_ISAAC_SIM → isaac_rl.py  → isaac_envs/{task}.py  → IsaacEnv
```
