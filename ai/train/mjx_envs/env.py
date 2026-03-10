"""Generic term-based MJX environment.

``MJXEnvCfg`` declares the MDP as composable terms (rewards, observations,
terminations, reset) -- mirroring Isaac Lab's ``ManagerBasedRLEnvCfg``
pattern but using pure-JAX callables compatible with ``jax.vmap`` /
``jax.jit``.

A single ``MJXEnv`` class interprets any config:  the ``step()`` and
``reset()`` methods are fully generic, so adding a new task only
requires writing a new config (no new env class needed).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional, Tuple

import jax
import jax.numpy as jnp
import mujoco
from mujoco import mjx

from ai.train.mjx_envs.base_env import EnvState, StepResult
from ai.train.mjx_envs.terms.observations import ObservationFn
from ai.train.mjx_envs.terms.resets import ResetFn
from ai.train.mjx_envs.terms.rewards import RewardFn
from ai.train.mjx_envs.terms.terminations import TerminationFn


@dataclass
class RewardTerm:
    func: RewardFn
    weight: float = 1.0


@dataclass
class MJXEnvCfg:
    """Declarative MDP specification for an MJX environment.

    Mirrors the structure of Isaac Lab's ``ManagerBasedRLEnvCfg``::

        rewards:      weighted sum  -> scalar reward
        observations: concatenated  -> obs vector
        terminations: OR'd          -> done flag
        reset_fn:     auto-reset    -> new (data, target_pos)
    """
    frame_skip: int = 10
    max_episode_steps: int = 500

    rewards: List[RewardTerm] = field(default_factory=list)
    observations: List[ObservationFn] = field(default_factory=list)
    terminations: List[TerminationFn] = field(default_factory=list)
    reset_fn: Optional[ResetFn] = None


class MJXEnv:
    """Generic MJX environment driven entirely by an ``MJXEnvCfg``.

    The ``step`` / ``reset`` methods compose terms from the config at
    trace time (Python for-loops unroll during ``jax.jit``).
    """

    def __init__(self, cfg: MJXEnvCfg, mj_model: mujoco.MjModel) -> None:
        self.mj_model = mj_model
        self.mjx_model = mjx.put_model(mj_model)
        self._frame_skip = cfg.frame_skip
        self._max_steps = cfg.max_episode_steps
        self._dt = mj_model.opt.timestep * cfg.frame_skip
        self._init_data = mjx.make_data(self.mjx_model)

        self.action_size: int = mj_model.nu
        self.action_low = jnp.array(mj_model.actuator_ctrlrange[:, 0])
        self.action_high = jnp.array(mj_model.actuator_ctrlrange[:, 1])

        self._reward_terms = cfg.rewards
        self._obs_fns = cfg.observations
        self._term_fns = cfg.terminations
        self._reset_fn = cfg.reset_fn

        self.obs_size = self._compute_obs_size()

    # ── internal helpers ──────────────────────────────────────────────

    def _compute_obs_size(self) -> int:
        dummy_data = mjx.forward(self.mjx_model, self._init_data)
        dummy_state = EnvState(
            dummy_data, jnp.zeros(3), jnp.int32(0), jax.random.PRNGKey(0)
        )
        parts = [fn(dummy_data, dummy_state) for fn in self._obs_fns]
        return sum(p.shape[0] for p in parts)

    def _compute_obs(self, data: mjx.Data, state: EnvState) -> jax.Array:
        return jnp.concatenate([fn(data, state) for fn in self._obs_fns])

    def _compute_reward(
        self, data: mjx.Data, state: EnvState, action: jax.Array
    ) -> jax.Array:
        reward = jnp.float32(0.0)
        for term in self._reward_terms:
            reward = reward + term.weight * term.func(data, state, action)
        return reward

    def _compute_terminated(
        self, data: mjx.Data, state: EnvState
    ) -> jax.Array:
        terminated = jnp.bool_(False)
        for fn in self._term_fns:
            terminated = terminated | fn(data, state)
        return terminated

    def _physics_step(self, data: mjx.Data) -> mjx.Data:
        def _step(d, _):
            return mjx.step(self.mjx_model, d), None
        data, _ = jax.lax.scan(_step, data, None, length=self._frame_skip)
        return data

    # ── public API ─────────────────────────────────────────────────────

    def reset(self, rng: jax.Array) -> Tuple[EnvState, jax.Array]:
        rng, rng_reset = jax.random.split(rng)
        data, target_pos = self._reset_fn(
            self.mjx_model, self._init_data, rng_reset
        )
        state = EnvState(
            mjx_data=data,
            target_pos=target_pos,
            step_count=jnp.int32(0),
            rng=rng,
        )
        obs = self._compute_obs(data, state)
        return state, obs

    def step(self, state: EnvState, action: jax.Array) -> StepResult:
        action = jnp.clip(action, self.action_low, self.action_high)
        data = state.mjx_data.replace(ctrl=action)
        data = self._physics_step(data)

        reward = self._compute_reward(data, state, action)
        terminated = self._compute_terminated(data, state)
        step_count = state.step_count + 1
        truncated = step_count >= self._max_steps
        done = terminated | truncated

        stepped_state = EnvState(
            mjx_data=data,
            target_pos=state.target_pos,
            step_count=step_count,
            rng=state.rng,
        )
        stepped_obs = self._compute_obs(data, stepped_state)

        rng, rng_reset = jax.random.split(state.rng)
        reset_data, reset_target = self._reset_fn(
            self.mjx_model, self._init_data, rng_reset
        )
        reset_state = EnvState(
            mjx_data=reset_data,
            target_pos=reset_target,
            step_count=jnp.int32(0),
            rng=rng,
        )
        reset_obs = self._compute_obs(reset_data, reset_state)

        final_state = jax.tree.map(
            lambda r, c: jnp.where(done, r, c), reset_state, stepped_state
        )
        obs = jnp.where(done, reset_obs, stepped_obs)
        return StepResult(state=final_state, obs=obs, reward=reward, done=done)
