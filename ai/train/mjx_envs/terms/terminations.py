"""Reusable MJX termination term factories.

Each factory captures static config and returns a pure-JAX function::

    fn(data: mjx.Data, state: EnvState) -> jax.Array  # bool scalar

Note: episode *truncation* (time-out) is handled generically by
``MJXEnv`` and does not need a term here.
"""

from __future__ import annotations

from typing import Callable

import jax
import jax.numpy as jnp
from mujoco import mjx

from ai.train.mjx_envs.base_env import EnvState

TerminationFn = Callable[[mjx.Data, EnvState], jax.Array]


def height_out_of_range(
    z_low: float,
    z_high: float,
    qpos_index: int = 2,
) -> TerminationFn:
    """Terminate when a qpos element leaves a healthy range."""
    def fn(data: mjx.Data, state: EnvState) -> jax.Array:
        z = data.qpos[qpos_index]
        return ~((z > z_low) & (z < z_high))
    return fn


def body_target_reached(body_id: int, threshold: float) -> TerminationFn:
    """Terminate when a body is within *threshold* of ``state.target_pos``."""
    def fn(data: mjx.Data, state: EnvState) -> jax.Array:
        dist = jnp.linalg.norm(data.xpos[body_id] - state.target_pos)
        return dist < threshold
    return fn


def object_target_reached(obj_body_id: int, threshold: float) -> TerminationFn:
    """Terminate when an object body is within *threshold* of target."""
    def fn(data: mjx.Data, state: EnvState) -> jax.Array:
        dist = jnp.linalg.norm(data.xpos[obj_body_id] - state.target_pos)
        return dist < threshold
    return fn
