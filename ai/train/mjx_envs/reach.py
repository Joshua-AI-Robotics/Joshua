"""MJX reach task: move end-effector to a random 3D target."""

from __future__ import annotations

from typing import Tuple

import jax
import jax.numpy as jnp
from mujoco import mjx

from ai.train.mjx_envs.base_env import EnvState, MJXBaseEnv, StepResult

import mujoco


class Env(MJXBaseEnv):
    """Reach task: move end-effector to a random 3D target."""

    def __init__(
        self,
        model_path: str,
        frame_skip: int = 10,
        max_episode_steps: int = 500,
    ) -> None:
        super().__init__(model_path, frame_skip, max_episode_steps)

        self._ee_body_id = mujoco.mj_name2id(
            self.mj_model, mujoco.mjtObj.mjOBJ_BODY, "Fixed_Jaw"
        )

        self.obs_size = self.mj_model.nq + self.mj_model.nv + 3 + 3

        self._ws_low = jnp.array([-0.15, -0.20, 0.01])
        self._ws_high = jnp.array([0.15, -0.05, 0.25])
        self._success_thresh = 0.02

    def _get_obs(self, data: mjx.Data, target_pos: jax.Array) -> jax.Array:
        ee_pos = data.xpos[self._ee_body_id]
        return jnp.concatenate([data.qpos, data.qvel, ee_pos, target_pos])

    def reset(self, rng: jax.Array) -> Tuple[EnvState, jax.Array]:
        rng, rng_target = jax.random.split(rng)
        target_pos = jax.random.uniform(
            rng_target, shape=(3,), minval=self._ws_low, maxval=self._ws_high
        )
        data = self._init_data
        data = mjx.forward(self.mjx_model, data)
        state = EnvState(
            mjx_data=data,
            target_pos=target_pos,
            step_count=jnp.int32(0),
            rng=rng,
        )
        obs = self._get_obs(data, target_pos)
        return state, obs

    def _reset_if_done(
        self, state: EnvState, done: jax.Array
    ) -> Tuple[EnvState, jax.Array]:
        rng, rng_target = jax.random.split(state.rng)
        target_pos = jax.random.uniform(
            rng_target, shape=(3,), minval=self._ws_low, maxval=self._ws_high
        )
        reset_data = mjx.forward(self.mjx_model, self._init_data)
        reset_obs = self._get_obs(reset_data, target_pos)
        reset_state = EnvState(
            mjx_data=reset_data,
            target_pos=target_pos,
            step_count=jnp.int32(0),
            rng=rng,
        )

        new_state = jax.tree.map(
            lambda r, c: jnp.where(done, r, c), reset_state, state
        )
        obs = jnp.where(
            done, reset_obs, self._get_obs(state.mjx_data, state.target_pos)
        )
        return new_state, obs

    def step(self, state: EnvState, action: jax.Array) -> StepResult:
        action = jnp.clip(action, self.action_low, self.action_high)
        data = state.mjx_data.replace(ctrl=action)
        data = self._physics_step(data)

        ee_pos = data.xpos[self._ee_body_id]
        dist = jnp.linalg.norm(ee_pos - state.target_pos)
        reward = -dist
        terminated = dist < self._success_thresh
        step_count = state.step_count + 1
        truncated = step_count >= self._max_steps
        done = terminated | truncated

        new_state = EnvState(
            mjx_data=data,
            target_pos=state.target_pos,
            step_count=step_count,
            rng=state.rng,
        )
        new_state, obs = self._reset_if_done(new_state, done)
        return StepResult(state=new_state, obs=obs, reward=reward, done=done)
