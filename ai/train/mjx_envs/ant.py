"""MJX ant locomotion: 4-legged ant walking forward (+x direction).

Standard Ant-v4 reward: forward velocity + healthy bonus - control cost.
Observation: qpos[2:] (skip x,y) + qvel = 27 dims.
"""

from __future__ import annotations

from typing import Tuple

import jax
import jax.numpy as jnp
from mujoco import mjx

from ai.train.mjx_envs.base_env import EnvState, MJXBaseEnv, StepResult


class Env(MJXBaseEnv):
    """Ant locomotion: 4-legged ant walking forward (+x direction)."""

    _CTRL_COST_WEIGHT = 0.5
    _HEALTHY_REWARD = 1.0
    _HEALTHY_Z_LOW = 0.2
    _HEALTHY_Z_HIGH = 1.0

    def __init__(
        self,
        model_path: str,
        frame_skip: int = 5,
        max_episode_steps: int = 1000,
    ) -> None:
        super().__init__(model_path, frame_skip, max_episode_steps)
        self.obs_size = self.mj_model.nq - 2 + self.mj_model.nv

    def _get_obs(self, data: mjx.Data) -> jax.Array:
        return jnp.concatenate([data.qpos[2:], data.qvel])

    def _make_reset_data(self, rng: jax.Array) -> mjx.Data:
        rng_q, rng_v = jax.random.split(rng)
        nq, nv = self.mj_model.nq, self.mj_model.nv

        qpos = self._init_data.qpos
        qpos = qpos.at[:3].set(jnp.array([0.0, 0.0, 0.75]))
        qpos = qpos.at[3:7].set(jnp.array([1.0, 0.0, 0.0, 0.0]))
        joint_noise = jax.random.uniform(
            rng_q, shape=(nq - 7,), minval=-0.1, maxval=0.1
        )
        qpos = qpos.at[7:].add(joint_noise)

        qvel = jax.random.uniform(rng_v, shape=(nv,), minval=-0.1, maxval=0.1)

        data = self._init_data.replace(qpos=qpos, qvel=qvel)
        return mjx.forward(self.mjx_model, data)

    def reset(self, rng: jax.Array) -> Tuple[EnvState, jax.Array]:
        rng, rng_reset = jax.random.split(rng)
        data = self._make_reset_data(rng_reset)
        state = EnvState(
            mjx_data=data,
            target_pos=jnp.zeros(3),
            step_count=jnp.int32(0),
            rng=rng,
        )
        return state, self._get_obs(data)

    def step(self, state: EnvState, action: jax.Array) -> StepResult:
        action = jnp.clip(action, self.action_low, self.action_high)
        data = state.mjx_data.replace(ctrl=action)
        x_before = data.qpos[0]

        data = self._physics_step(data)

        x_velocity = (data.qpos[0] - x_before) / self._dt

        z = data.qpos[2]
        is_healthy = (z > self._HEALTHY_Z_LOW) & (z < self._HEALTHY_Z_HIGH)
        forward_reward = x_velocity
        healthy_reward = self._HEALTHY_REWARD * is_healthy
        ctrl_cost = self._CTRL_COST_WEIGHT * jnp.sum(action**2)
        reward = forward_reward + healthy_reward - ctrl_cost

        step_count = state.step_count + 1
        done = (~is_healthy) | (step_count >= self._max_steps)

        stepped_state = EnvState(
            mjx_data=data,
            target_pos=jnp.zeros(3),
            step_count=step_count,
            rng=state.rng,
        )
        stepped_obs = self._get_obs(data)

        rng, rng_reset = jax.random.split(state.rng)
        reset_data = self._make_reset_data(rng_reset)
        reset_state = EnvState(
            mjx_data=reset_data,
            target_pos=jnp.zeros(3),
            step_count=jnp.int32(0),
            rng=rng,
        )
        reset_obs = self._get_obs(reset_data)

        new_state = jax.tree.map(
            lambda r, c: jnp.where(done, r, c), reset_state, stepped_state
        )
        obs = jnp.where(done, reset_obs, stepped_obs)
        return StepResult(state=new_state, obs=obs, reward=reward, done=done)
