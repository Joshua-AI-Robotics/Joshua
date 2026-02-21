"""MJX (MuJoCo XLA) environment wrappers for GPU-parallel training.

Pure-JAX implementations of reach and pick-place tasks. All state is
explicit (no mutable Python objects) so that reset/step can be vmap'd
across thousands of environments on a single GPU.
"""

from __future__ import annotations

from typing import NamedTuple, Tuple

import jax
import jax.numpy as jnp
import mujoco
from mujoco import mjx


class EnvState(NamedTuple):
    mjx_data: mjx.Data
    target_pos: jax.Array
    step_count: jax.Array
    rng: jax.Array


class StepResult(NamedTuple):
    state: EnvState
    obs: jax.Array
    reward: jax.Array
    done: jax.Array


class MJXReachEnv:
    """Reach task: move end-effector to a random 3D target.

    Mirrors simulation/envs/reach.py but in pure JAX for GPU batching.
    """

    def __init__(self, model_path: str, frame_skip: int = 10,
                 max_episode_steps: int = 500) -> None:
        self.mj_model = mujoco.MjModel.from_xml_path(model_path)
        self.mjx_model = mjx.put_model(self.mj_model)
        self._frame_skip = frame_skip
        self._max_steps = max_episode_steps
        self._init_data = mjx.make_data(self.mjx_model)

        self._ee_body_id = mujoco.mj_name2id(
            self.mj_model, mujoco.mjtObj.mjOBJ_BODY, "Fixed_Jaw"
        )

        self.obs_size = self.mj_model.nq + self.mj_model.nv + 3 + 3
        self.action_size = self.mj_model.nu
        self.action_low = jnp.array(self.mj_model.actuator_ctrlrange[:, 0])
        self.action_high = jnp.array(self.mj_model.actuator_ctrlrange[:, 1])

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

    def _reset_if_done(self, state: EnvState, done: jax.Array) -> Tuple[EnvState, jax.Array]:
        """Lightweight reset: reuse pre-built init data instead of mjx.make_data."""
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
        obs = jnp.where(done, reset_obs, self._get_obs(state.mjx_data, state.target_pos))
        return new_state, obs

    def step(self, state: EnvState, action: jax.Array) -> StepResult:
        action = jnp.clip(action, self.action_low, self.action_high)
        data = state.mjx_data.replace(ctrl=action)

        def _physics_step(d, _):
            return mjx.step(self.mjx_model, d), None

        data, _ = jax.lax.scan(_physics_step, data, None, length=self._frame_skip)

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


class MJXPickPlaceEnv:
    """Pick-and-place task: grasp object and move to target.

    Mirrors simulation/envs/pick_place.py but in pure JAX for GPU batching.
    """

    def __init__(self, model_path: str, frame_skip: int = 10,
                 max_episode_steps: int = 1000) -> None:
        self.mj_model = mujoco.MjModel.from_xml_path(model_path)
        self.mjx_model = mjx.put_model(self.mj_model)
        self._frame_skip = frame_skip
        self._max_steps = max_episode_steps
        self._init_data = mjx.make_data(self.mjx_model)

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
        self.action_size = self.mj_model.nu
        self.action_low = jnp.array(self.mj_model.actuator_ctrlrange[:, 0])
        self.action_high = jnp.array(self.mj_model.actuator_ctrlrange[:, 1])

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
        """Build reset data from cached init_data (avoids re-tracing mjx.make_data)."""
        rng_obj, rng_tgt = jax.random.split(rng)
        obj_pos = jax.random.uniform(
            rng_obj, shape=(3,), minval=self._obj_spawn_low, maxval=self._obj_spawn_high
        )
        target_pos = jax.random.uniform(
            rng_tgt, shape=(3,), minval=self._tgt_spawn_low, maxval=self._tgt_spawn_high
        )
        data = self._init_data
        qpos = data.qpos.at[self._obj_qpos_adr:self._obj_qpos_adr + 3].set(obj_pos)
        qpos = qpos.at[self._obj_qpos_adr + 3:self._obj_qpos_adr + 7].set(
            jnp.array([1.0, 0.0, 0.0, 0.0])
        )
        qvel = data.qvel.at[self._obj_dof_adr:self._obj_dof_adr + 6].set(0.0)
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

        def _physics_step(d, _):
            return mjx.step(self.mjx_model, d), None

        data, _ = jax.lax.scan(_physics_step, data, None, length=self._frame_skip)

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


class MJXAntEnv:
    """Ant locomotion: 4-legged ant walking forward (+x direction).

    Standard Ant-v4 reward: forward velocity + healthy bonus - control cost.
    Observation: qpos[2:] (skip x,y) + qvel = 27 dims.
    """

    _CTRL_COST_WEIGHT = 0.5
    _HEALTHY_REWARD = 1.0
    _HEALTHY_Z_LOW = 0.2
    _HEALTHY_Z_HIGH = 1.0

    def __init__(self, model_path: str, frame_skip: int = 5,
                 max_episode_steps: int = 1000) -> None:
        self.mj_model = mujoco.MjModel.from_xml_path(model_path)
        self.mjx_model = mjx.put_model(self.mj_model)
        self._frame_skip = frame_skip
        self._max_steps = max_episode_steps
        self._dt = self.mj_model.opt.timestep * frame_skip
        self._init_data = mjx.make_data(self.mjx_model)

        self.obs_size = self.mj_model.nq - 2 + self.mj_model.nv
        self.action_size = self.mj_model.nu
        self.action_low = jnp.array(self.mj_model.actuator_ctrlrange[:, 0])
        self.action_high = jnp.array(self.mj_model.actuator_ctrlrange[:, 1])

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

        def _physics_step(d, _):
            return mjx.step(self.mjx_model, d), None

        data, _ = jax.lax.scan(_physics_step, data, None, length=self._frame_skip)

        x_velocity = (data.qpos[0] - x_before) / self._dt

        z = data.qpos[2]
        is_healthy = (z > self._HEALTHY_Z_LOW) & (z < self._HEALTHY_Z_HIGH)
        forward_reward = x_velocity
        healthy_reward = self._HEALTHY_REWARD * is_healthy
        ctrl_cost = self._CTRL_COST_WEIGHT * jnp.sum(action ** 2)
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


TASK_ENVS = {
    "reach": MJXReachEnv,
    "pick_place": MJXPickPlaceEnv,
    "ant": MJXAntEnv,
}
