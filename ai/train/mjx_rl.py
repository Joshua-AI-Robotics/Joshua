"""CleanRL-style PPO for MJX GPU-parallel training.

Single-file implementation: actor-critic network, GAE, clipped PPO loss,
all running on GPU via JAX. Paired with MJX environments from mjx_envs.py.

Usage (through trainer.py):
    bazel run //ai/train:trainer -- \
        --config config/config_preset/so_arm100_train_mjx.pbtxt
"""

from __future__ import annotations

import functools
import json
import os
import time
from typing import NamedTuple, Optional

import flax.linen as nn
import jax
import jax.numpy as jnp
import mujoco
import mujoco.viewer
import numpy as np
import optax
from flax.training import train_state

import glog

from ai.proto import training_pb2
from ai.train.mjx_envs import TASK_ENVS, EnvState, StepResult

os.environ.setdefault("JAX_COMPILATION_CACHE_DIR", "/tmp/jax_cache")
os.environ.setdefault("JAX_PERSISTENT_CACHE_MIN_ENTRY_SIZE_BYTES", "0")
os.environ.setdefault("JAX_PERSISTENT_CACHE_MIN_COMPILE_TIME_SECS", "0")

_CHECKPOINT_DIR = "/tmp/joshua_checkpoints"
os.makedirs(_CHECKPOINT_DIR, exist_ok=True)


def _checkpoint_path(name: str) -> str:
    """Resolve a checkpoint path under the persistent checkpoint dir."""
    if os.path.isabs(name):
        return name
    return os.path.join(_CHECKPOINT_DIR, name)

# ── Hyperparameters (sensible defaults, overridden by config) ─────────

_LR = 3e-4
_GAMMA = 0.99
_GAE_LAMBDA = 0.95
_CLIP_EPS = 0.2
_VF_COEFF = 0.5
_ENT_COEFF = 0.01
_MAX_GRAD_NORM = 0.5
_NUM_MINIBATCHES = 4
_UPDATE_EPOCHS = 4
_ROLLOUT_LENGTH = 128


# ── Actor-Critic Network ─────────────────────────────────────────────

class ActorCritic(nn.Module):
    action_dim: int

    @nn.compact
    def __call__(self, obs):
        x = nn.Dense(256)(obs)
        x = nn.relu(x)
        x = nn.Dense(256)(x)
        x = nn.relu(x)

        mean = nn.Dense(self.action_dim)(x)
        log_std = self.param(
            "log_std", nn.initializers.zeros, (self.action_dim,)
        )
        value = nn.Dense(1)(x)
        return mean, log_std, jnp.squeeze(value, -1)


# ── Rollout storage ──────────────────────────────────────────────────

class Transition(NamedTuple):
    obs: jax.Array
    action: jax.Array
    reward: jax.Array
    done: jax.Array
    log_prob: jax.Array
    value: jax.Array


class RunnerState(NamedTuple):
    train_state: train_state.TrainState
    env_state: EnvState
    obs: jax.Array
    rng: jax.Array


# ── Core PPO functions ───────────────────────────────────────────────

def _sample_action(params, apply_fn, obs, rng):
    mean, log_std, value = apply_fn(params, obs)
    std = jnp.exp(log_std)
    action = mean + std * jax.random.normal(rng, shape=mean.shape)
    log_prob = -0.5 * (
        jnp.sum(((action - mean) / std) ** 2 + 2 * log_std + jnp.log(2 * jnp.pi), axis=-1)
    )
    return action, log_prob, value


def _compute_gae(rewards, values, dones, last_value, gamma, gae_lambda):
    def _scan_fn(carry, transition):
        next_value, gae = carry
        reward, value, done = transition
        delta = reward + gamma * next_value * (1 - done) - value
        gae = delta + gamma * gae_lambda * (1 - done) * gae
        return (value, gae), gae

    _, advantages = jax.lax.scan(
        _scan_fn,
        (last_value, jnp.zeros_like(last_value)),
        (rewards[::-1], values[::-1], dones[::-1]),
    )
    advantages = advantages[::-1]
    returns = advantages + values
    return advantages, returns


def _ppo_loss(params, apply_fn, batch, clip_eps, vf_coeff, ent_coeff):
    obs, actions, old_log_probs, advantages, returns = batch

    mean, log_std, values = apply_fn(params, obs)
    std = jnp.exp(log_std)
    log_probs = -0.5 * jnp.sum(
        ((actions - mean) / std) ** 2 + 2 * log_std + jnp.log(2 * jnp.pi), axis=-1
    )
    entropy = 0.5 * jnp.sum(log_std + 0.5 * jnp.log(2 * jnp.pi * jnp.e), axis=-1)

    ratio = jnp.exp(log_probs - old_log_probs)
    adv_normalized = (advantages - advantages.mean()) / (advantages.std() + 1e-8)
    loss_clip = -jnp.minimum(
        ratio * adv_normalized,
        jnp.clip(ratio, 1 - clip_eps, 1 + clip_eps) * adv_normalized,
    ).mean()
    loss_vf = ((values - returns) ** 2).mean()
    loss_ent = -entropy.mean()

    return loss_clip + vf_coeff * loss_vf + ent_coeff * loss_ent


# ── Live viewer for GPU training ──────────────────────────────────────

class _MJXViewer:
    """Copies env 0 state from GPU to CPU and updates a MuJoCo viewer."""

    def __init__(self, mj_model: mujoco.MjModel) -> None:
        self._model = mj_model
        self._data = mujoco.MjData(mj_model)
        self._viewer = mujoco.viewer.launch_passive(
            self._model, self._data, show_left_ui=False, show_right_ui=False,
        )

    def update(self, env_state: EnvState) -> None:
        qpos = np.array(jax.device_get(env_state.mjx_data.qpos[0]))
        qvel = np.array(jax.device_get(env_state.mjx_data.qvel[0]))
        self._data.qpos[:] = qpos
        self._data.qvel[:] = qvel
        mujoco.mj_forward(self._model, self._data)
        self._viewer.sync()

    @property
    def is_running(self) -> bool:
        return self._viewer.is_running()

    def close(self) -> None:
        self._viewer.close()


# ── Training loop ────────────────────────────────────────────────────

def run(config: training_pb2.RLConfig) -> None:
    task_name = config.task or "reach"
    env_cls = TASK_ENVS.get(task_name)
    if env_cls is None:
        raise ValueError(
            f"Unknown MJX task '{task_name}'. Available: {', '.join(TASK_ENVS)}"
        )

    num_envs = config.num_envs or 2048
    total_timesteps = config.total_timesteps or 10_000_000
    frame_skip = config.frame_skip or 10
    save_path = config.save_path or f"{task_name}_mjx_ppo"

    _MJX_MODELS = {
        "reach": "simulation/models/so_arm100_reach_mjx.xml",
        "pick_place": "simulation/models/so_arm100_pick_place_mjx.xml",
        "ant": "simulation/models/ant_mjx.xml",
    }
    model_path = config.checkpoint_path or _MJX_MODELS.get(task_name, "")
    if not model_path:
        raise ValueError(f"No MJX model path for task '{task_name}'")

    save_path = _checkpoint_path(save_path)

    glog.info(f"MJX PPO: task={task_name}, num_envs={num_envs}, "
              f"total_timesteps={total_timesteps}, model={model_path}")

    env = env_cls(model_path=model_path, frame_skip=frame_skip)

    num_updates = total_timesteps // (num_envs * _ROLLOUT_LENGTH)
    glog.info(f"  obs_size={env.obs_size}, action_size={env.action_size}")
    glog.info(f"  rollout_length={_ROLLOUT_LENGTH}, num_updates={num_updates}")

    rng = jax.random.PRNGKey(42)

    network = ActorCritic(action_dim=env.action_size)
    rng, rng_init = jax.random.split(rng)
    dummy_obs = jnp.zeros(env.obs_size)
    params = network.init(rng_init, dummy_obs)

    tx = optax.chain(
        optax.clip_by_global_norm(_MAX_GRAD_NORM),
        optax.adam(_LR, eps=1e-5),
    )
    ts = train_state.TrainState.create(
        apply_fn=network.apply, params=params, tx=tx
    )

    rng, *rng_envs = jax.random.split(rng, num_envs + 1)
    rng_envs = jnp.array(rng_envs)
    v_reset = jax.vmap(env.reset)
    env_states, obs = v_reset(rng_envs)

    v_step = jax.vmap(env.step)
    v_sample = jax.vmap(_sample_action, in_axes=(None, None, 0, 0))

    def _rollout_step(carry, _):
        ts_, env_state_, obs_, rng_ = carry
        rng_, rng_act = jax.random.split(rng_)
        rng_acts = jax.random.split(rng_act, num_envs)

        actions, log_probs, values = v_sample(
            ts_.params, ts_.apply_fn, obs_, rng_acts
        )
        results: StepResult = v_step(env_state_, actions)

        transition = Transition(
            obs=obs_,
            action=actions,
            reward=results.reward,
            done=results.done,
            log_prob=log_probs,
            value=values,
        )
        return (ts_, results.state, results.obs, rng_), transition

    @jax.jit
    def _update(runner_state: RunnerState):
        ts_, env_state_, obs_, rng_ = runner_state

        (ts_, env_state_, obs_, rng_), rollout = jax.lax.scan(
            _rollout_step,
            (ts_, env_state_, obs_, rng_),
            None,
            length=_ROLLOUT_LENGTH,
        )

        _, _, last_values = jax.vmap(
            lambda obs: ts_.apply_fn(ts_.params, obs)
        )(obs_)

        advantages, returns = _compute_gae(
            rollout.reward, rollout.value, rollout.done,
            last_values, _GAMMA, _GAE_LAMBDA,
        )

        batch_size = _ROLLOUT_LENGTH * num_envs
        minibatch_size = batch_size // _NUM_MINIBATCHES

        flat = lambda x: x.reshape((batch_size,) + x.shape[2:])
        batch_obs = flat(rollout.obs)
        batch_actions = flat(rollout.action)
        batch_log_probs = flat(rollout.log_prob)
        batch_advantages = flat(advantages)
        batch_returns = flat(returns)

        def _epoch(carry, _):
            ts_e, rng_e = carry
            rng_e, rng_perm = jax.random.split(rng_e)
            perm = jax.random.permutation(rng_perm, batch_size)

            def _minibatch(ts_m, start_idx):
                idx = jax.lax.dynamic_slice(perm, (start_idx,), (minibatch_size,))
                mb = (
                    batch_obs[idx],
                    batch_actions[idx],
                    batch_log_probs[idx],
                    batch_advantages[idx],
                    batch_returns[idx],
                )
                grads = jax.grad(_ppo_loss)(
                    ts_m.params, ts_m.apply_fn, mb,
                    _CLIP_EPS, _VF_COEFF, _ENT_COEFF,
                )
                return ts_m.apply_gradients(grads=grads), None

            starts = jnp.arange(0, batch_size, minibatch_size)
            ts_e, _ = jax.lax.scan(_minibatch, ts_e, starts)
            return (ts_e, rng_e), None

        rng_, rng_epoch = jax.random.split(rng_)
        (ts_, _), _ = jax.lax.scan(
            _epoch, (ts_, rng_epoch), None, length=_UPDATE_EPOCHS
        )

        return RunnerState(ts_, env_state_, obs_, rng_)

    viewer: Optional[_MJXViewer] = None
    if config.render:
        glog.info("Opening MuJoCo viewer for live preview (env 0) ...")
        viewer = _MJXViewer(env.mj_model)

    runner = RunnerState(ts, env_states, obs, rng)

    glog.info("JIT-compiling _update (this may take a few minutes on first run) ...")
    t_compile = time.time()
    runner = _update(runner)
    jax.block_until_ready(runner)
    glog.info(f"Compilation + first update done in {time.time() - t_compile:.1f}s")

    glog.info("Starting MJX PPO training ...")
    t0 = time.time()

    for update in range(2, num_updates + 1):
        runner = _update(runner)

        if viewer is not None:
            if not viewer.is_running:
                glog.info("Viewer closed, stopping training.")
                break
            viewer.update(runner.env_state)

        if update % 10 == 0 or update == num_updates:
            elapsed = time.time() - t0
            steps_done = update * num_envs * _ROLLOUT_LENGTH
            sps = steps_done / elapsed
            glog.info(
                f"  update {update}/{num_updates}  "
                f"steps={steps_done:,}  "
                f"SPS={sps:,.0f}  "
                f"elapsed={elapsed:.1f}s"
            )

    if viewer is not None:
        viewer.close()

    total_elapsed = time.time() - t0
    total_steps = num_updates * num_envs * _ROLLOUT_LENGTH
    glog.info(f"Training complete: {total_steps:,} steps in {total_elapsed:.1f}s "
              f"({total_steps / total_elapsed:,.0f} SPS)")

    params_np = jax.device_get(
        jax.tree.map(lambda x: np.array(x), runner.train_state.params)
    )
    import flax.serialization
    with open(save_path + ".msgpack", "wb") as f:
        f.write(flax.serialization.to_bytes(params_np))

    meta = {
        "task": task_name,
        "obs_size": env.obs_size,
        "action_size": env.action_size,
        "model_path": model_path,
        "total_timesteps": int(total_steps),
        "num_envs": num_envs,
        "frame_skip": frame_skip,
    }
    with open(save_path + "_meta.json", "w") as f:
        json.dump(meta, f, indent=2)

    glog.info(f"Parameters saved to {save_path}.msgpack  "
              f"(meta: {save_path}_meta.json)")


# ── Evaluation ────────────────────────────────────────────────────────

def eval_mjx(config: training_pb2.RLConfig) -> None:
    """Load a trained MJX PPO checkpoint and run with the viewer."""
    checkpoint_path = config.checkpoint_path
    if not checkpoint_path:
        raise ValueError("checkpoint_path is required for evaluation")
    checkpoint_path = _checkpoint_path(checkpoint_path)

    meta_path = checkpoint_path.replace(".msgpack", "_meta.json")
    try:
        with open(meta_path) as f:
            meta = json.load(f)
    except FileNotFoundError:
        meta = {}

    task_name = config.task or meta.get("task", "ant")
    env_cls = TASK_ENVS.get(task_name)
    if env_cls is None:
        raise ValueError(
            f"Unknown MJX task '{task_name}'. Available: {', '.join(TASK_ENVS)}"
        )

    _MJX_MODELS = {
        "reach": "simulation/models/so_arm100_reach_mjx.xml",
        "pick_place": "simulation/models/so_arm100_pick_place_mjx.xml",
        "ant": "simulation/models/ant_mjx.xml",
    }
    model_path = meta.get("model_path") or _MJX_MODELS.get(task_name, "")
    frame_skip = config.frame_skip or meta.get("frame_skip", 5)

    glog.info(f"MJX eval: task={task_name}, model={model_path}, "
              f"checkpoint={checkpoint_path}")

    env = env_cls(model_path=model_path, frame_skip=frame_skip)
    network = ActorCritic(action_dim=env.action_size)

    rng = jax.random.PRNGKey(0)
    dummy_params = network.init(rng, jnp.zeros(env.obs_size))

    import flax.serialization
    with open(checkpoint_path, "rb") as f:
        params = flax.serialization.from_bytes(dummy_params, f.read())

    @jax.jit
    def _deterministic_action(params, obs):
        mean, _, _ = network.apply(params, obs)
        return mean

    num_episodes = config.num_eval_episodes or 5
    viewer = _MJXViewer(env.mj_model)

    glog.info(f"Running {num_episodes} eval episodes (close viewer to stop) ...")

    for ep in range(num_episodes):
        rng, rng_reset = jax.random.split(rng)
        state, obs = env.reset(rng_reset)
        ep_reward = 0.0
        ep_steps = 0

        while True:
            if not viewer.is_running:
                glog.info("Viewer closed.")
                return

            action = _deterministic_action(params, obs)
            result = env.step(state, action)
            state, obs = result.state, result.obs
            ep_reward += float(jax.device_get(result.reward))
            ep_steps += 1

            viewer.update(state)

            if jax.device_get(result.done):
                break

        glog.info(f"  episode {ep + 1}/{num_episodes}: "
                  f"reward={ep_reward:.1f}, steps={ep_steps}")

    viewer.close()
    glog.info("Evaluation complete.")
