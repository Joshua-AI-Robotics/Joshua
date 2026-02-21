"""MJX pick-and-place task: grasp object and move to target."""

from __future__ import annotations

from typing import Tuple

import jax
import jax.numpy as jnp
from mujoco import mjx

from ai.train.mjx_envs.base_env import EnvState, MJXBaseEnv, StepResult

import mujoco


class Env(MJXBaseEnv):
    """Pick-and-place task: grasp object and move to target."""

    def __init__(
        self,
        model_path: str,
        frame_skip: int = 10,
        max_episode_steps: int = 1000,
    ) -> None:
        super().__init__(model_path, frame_skip, max_episode_steps)

        self._ee_body_id = mujoco.mj_name2id(
            self.mj_model, mujoco.mjtObj.mjOBJ_BODY, "Fixed_Jaw"
        )
        self._obj_body_id = mujoco.mj_name2id(
            self.mj_model, mujoco.mjtObj.mjOBJ_BODY, "pick_object"
        )
        self._obj_jnt_id = mujoco.mj_name2id(
            self.mj_model, mujoco.mjtObj.mjOBJ_JOINT, "object_joint"
        )
        self._obj_qpos_adr = int(self.mj_model.jnt_qposadr[self._obj_jnt_id])
        self._obj_dof_adr = int(self.mj_model.jnt_dofadr[self._obj_jnt_id])

        self.obs_size = self.mj_model.nq + self.mj_model.nv + 9
        self._obj_spawn_low = jnp.array([0.02, -0.18, 0.02])
        self._obj_spawn_high = jnp.array([0.12, -0.10, 0.02])
        self._tgt_spawn_low = jnp.array([-0.10, -0.18, 0.02])
        self._tgt_spawn_high = jnp.array([0.10, -0.08, 0.02])
        self._success_thresh = 0.03

    def _get_obs(self, data: mjx.Data, target_pos: jax.Array) -> jax.Array:
        ee_pos = data.xpos[self._ee_body_id]
        obj_pos = data.xpos[self._obj_body_id]
        return jnp.concatenate([data.qpos, data.qvel, ee_pos, obj_pos, target_pos])

    def _make_reset_data(self, rng: jax.Array) -> Tuple[mjx.Data, jax.Array]:
        rng_obj, rng_tgt = jax.random.split(rng)
        obj_pos = jax.random.uniform(
            rng_obj, shape=(3,), minval=self._obj_spawn_low, maxval=self._obj_spawn_high
        )
        target_pos = jax.random.uniform(
            rng_tgt, shape=(3,), minval=self._tgt_spawn_low, maxval=self._tgt_spawn_high
        )
        data = self._init_data
        qpos = data.qpos.at[self._obj_qpos_adr : self._obj_qpos_adr + 3].set(obj_pos)
        qpos = qpos.at[self._obj_qpos_adr + 3 : self._obj_qpos_adr + 7].set(
            jnp.array([1.0, 0.0, 0.0, 0.0])
        )
        qvel = data.qvel.at[self._obj_dof_adr : self._obj_dof_adr + 6].set(0.0)
        data = data.replace(qpos=qpos, qvel=qvel)
        data = mjx.forward(self.mjx_model, data)
        return data, target_pos

    def reset(self, rng: jax.Array) -> Tuple[EnvState, jax.Array]:
        rng, rng_reset = jax.random.split(rng)
        data, target_pos = self._make_reset_data(rng_reset)
        state = EnvState(
            mjx_data=data,
            target_pos=target_pos,
            step_count=jnp.int32(0),
            rng=rng,
        )
        obs = self._get_obs(data, target_pos)
        return state, obs

    def step(self, state: EnvState, action: jax.Array) -> StepResult:
        action = jnp.clip(action, self.action_low, self.action_high)
        data = state.mjx_data.replace(ctrl=action)
        data = self._physics_step(data)

        ee_pos = data.xpos[self._ee_body_id]
        obj_pos = data.xpos[self._obj_body_id]
        dist_ee_obj = jnp.linalg.norm(ee_pos - obj_pos)
        dist_obj_tgt = jnp.linalg.norm(obj_pos - state.target_pos)
        reward = -(dist_ee_obj + dist_obj_tgt)
        terminated = dist_obj_tgt < self._success_thresh
        step_count = state.step_count + 1
        truncated = step_count >= self._max_steps
        done = terminated | truncated

        stepped_state = EnvState(
            mjx_data=data,
            target_pos=state.target_pos,
            step_count=step_count,
            rng=state.rng,
        )
        stepped_obs = self._get_obs(data, state.target_pos)

        rng, rng_reset = jax.random.split(state.rng)
        reset_data, reset_target = self._make_reset_data(rng_reset)
        reset_state = EnvState(
            mjx_data=reset_data,
            target_pos=reset_target,
            step_count=jnp.int32(0),
            rng=rng,
        )
        reset_obs = self._get_obs(reset_data, reset_target)

        new_state = jax.tree.map(
            lambda r, c: jnp.where(done, r, c), reset_state, stepped_state
        )
        obs = jnp.where(done, reset_obs, stepped_obs)
        return StepResult(state=new_state, obs=obs, reward=reward, done=done)
