"""Reusable MJX reset function factories.

Each factory captures static config and returns a pure-JAX function::

    fn(mjx_model, init_data, rng) -> (mjx.Data, target_pos)

The returned ``target_pos`` is stored in ``EnvState.target_pos``.
Tasks without an explicit target should return ``jnp.zeros(3)``.
"""

from __future__ import annotations

from typing import Callable, Tuple

import jax
import jax.numpy as jnp
from mujoco import mjx

ResetFn = Callable[[mjx.Model, mjx.Data, jax.Array], Tuple[mjx.Data, jax.Array]]


def locomotion_reset(
    initial_height: float = 0.75,
    joint_noise: float = 0.1,
    velocity_noise: float = 0.1,
) -> ResetFn:
    """Reset for locomotion tasks (ant, humanoid, etc.).

    Randomises joint positions/velocities around a standing pose.
    """
    def fn(
        mjx_model: mjx.Model, init_data: mjx.Data, rng: jax.Array
    ) -> Tuple[mjx.Data, jax.Array]:
        rng_q, rng_v = jax.random.split(rng)
        nq = init_data.qpos.shape[0]
        nv = init_data.qvel.shape[0]

        qpos = init_data.qpos
        qpos = qpos.at[:3].set(jnp.array([0.0, 0.0, initial_height]))
        qpos = qpos.at[3:7].set(jnp.array([1.0, 0.0, 0.0, 0.0]))
        noise = jax.random.uniform(
            rng_q, shape=(nq - 7,), minval=-joint_noise, maxval=joint_noise
        )
        qpos = qpos.at[7:].add(noise)

        qvel = jax.random.uniform(
            rng_v, shape=(nv,), minval=-velocity_noise, maxval=velocity_noise
        )

        data = init_data.replace(qpos=qpos, qvel=qvel)
        data = mjx.forward(mjx_model, data)
        return data, jnp.zeros(3)
    return fn


def random_target_reset(
    ws_low: jax.Array,
    ws_high: jax.Array,
) -> ResetFn:
    """Reset for reach-style tasks: fixed robot, random target in workspace."""
    ws_low = jnp.asarray(ws_low)
    ws_high = jnp.asarray(ws_high)

    def fn(
        mjx_model: mjx.Model, init_data: mjx.Data, rng: jax.Array
    ) -> Tuple[mjx.Data, jax.Array]:
        _rng, rng_target = jax.random.split(rng)
        target_pos = jax.random.uniform(
            rng_target, shape=(3,), minval=ws_low, maxval=ws_high
        )
        data = mjx.forward(mjx_model, init_data)
        return data, target_pos
    return fn


def pick_place_reset(
    obj_qpos_adr: int,
    obj_dof_adr: int,
    obj_spawn_low: jax.Array,
    obj_spawn_high: jax.Array,
    tgt_spawn_low: jax.Array,
    tgt_spawn_high: jax.Array,
) -> ResetFn:
    """Reset for pick-and-place: random object + random target positions."""
    obj_spawn_low = jnp.asarray(obj_spawn_low)
    obj_spawn_high = jnp.asarray(obj_spawn_high)
    tgt_spawn_low = jnp.asarray(tgt_spawn_low)
    tgt_spawn_high = jnp.asarray(tgt_spawn_high)

    def fn(
        mjx_model: mjx.Model, init_data: mjx.Data, rng: jax.Array
    ) -> Tuple[mjx.Data, jax.Array]:
        rng_obj, rng_tgt = jax.random.split(rng)
        obj_pos = jax.random.uniform(
            rng_obj, shape=(3,), minval=obj_spawn_low, maxval=obj_spawn_high
        )
        target_pos = jax.random.uniform(
            rng_tgt, shape=(3,), minval=tgt_spawn_low, maxval=tgt_spawn_high
        )
        qpos = init_data.qpos.at[obj_qpos_adr : obj_qpos_adr + 3].set(obj_pos)
        qpos = qpos.at[obj_qpos_adr + 3 : obj_qpos_adr + 7].set(
            jnp.array([1.0, 0.0, 0.0, 0.0])
        )
        qvel = init_data.qvel.at[obj_dof_adr : obj_dof_adr + 6].set(0.0)
        data = init_data.replace(qpos=qpos, qvel=qvel)
        data = mjx.forward(mjx_model, data)
        return data, target_pos
    return fn
