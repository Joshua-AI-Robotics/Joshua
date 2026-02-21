"""Reusable MJX observation term factories.

Each factory captures static config and returns a pure-JAX function::

    fn(data: mjx.Data, state: EnvState) -> jax.Array  # 1-D
"""

from __future__ import annotations

from typing import Callable

import jax
import jax.numpy as jnp
from mujoco import mjx

from ai.train.mjx_envs.base_env import EnvState

ObservationFn = Callable[[mjx.Data, EnvState], jax.Array]


def qpos(skip: int = 0) -> ObservationFn:
    """Generalized positions, optionally skipping the first *skip* elements."""
    def fn(data: mjx.Data, state: EnvState) -> jax.Array:
        return data.qpos[skip:]
    return fn


def qvel() -> ObservationFn:
    """Generalized velocities."""
    def fn(data: mjx.Data, state: EnvState) -> jax.Array:
        return data.qvel
    return fn


def body_position(body_id: int) -> ObservationFn:
    """World-frame position of a body (3-D)."""
    def fn(data: mjx.Data, state: EnvState) -> jax.Array:
        return data.xpos[body_id]
    return fn


def target_position() -> ObservationFn:
    """Target position stored in ``state.target_pos`` (3-D)."""
    def fn(data: mjx.Data, state: EnvState) -> jax.Array:
        return state.target_pos
    return fn
