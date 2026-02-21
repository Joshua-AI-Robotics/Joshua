"""Reusable MJX reward term factories.

Each factory captures static config and returns a pure-JAX function::

    fn(data: mjx.Data, state: EnvState, action: jax.Array) -> jax.Array  # scalar
"""

from __future__ import annotations

from typing import Callable

import jax
import jax.numpy as jnp
from mujoco import mjx

from ai.train.mjx_envs.base_env import EnvState

RewardFn = Callable[[mjx.Data, EnvState, jax.Array], jax.Array]


def forward_velocity(dt: float) -> RewardFn:
    """X-axis velocity reward (e.g. locomotion tasks)."""
    def fn(data: mjx.Data, state: EnvState, action: jax.Array) -> jax.Array:
        return (data.qpos[0] - state.mjx_data.qpos[0]) / dt
    return fn


def healthy_bonus(
    z_low: float,
    z_high: float,
    bonus: float = 1.0,
    qpos_index: int = 2,
) -> RewardFn:
    """Constant bonus while a qpos element stays within a healthy range."""
    def fn(data: mjx.Data, state: EnvState, action: jax.Array) -> jax.Array:
        z = data.qpos[qpos_index]
        is_healthy = (z > z_low) & (z < z_high)
        return bonus * is_healthy.astype(jnp.float32)
    return fn


def action_l2() -> RewardFn:
    """Sum of squared actions (use with negative weight for penalty)."""
    def fn(data: mjx.Data, state: EnvState, action: jax.Array) -> jax.Array:
        return jnp.sum(action ** 2)
    return fn


def body_target_distance(body_id: int) -> RewardFn:
    """Euclidean distance from a body to ``state.target_pos``.

    Returns the positive distance -- pair with a negative weight to
    create an approach reward.
    """
    def fn(data: mjx.Data, state: EnvState, action: jax.Array) -> jax.Array:
        return jnp.linalg.norm(data.xpos[body_id] - state.target_pos)
    return fn


def body_body_distance(body_id_a: int, body_id_b: int) -> RewardFn:
    """Euclidean distance between two bodies."""
    def fn(data: mjx.Data, state: EnvState, action: jax.Array) -> jax.Array:
        return jnp.linalg.norm(data.xpos[body_id_a] - data.xpos[body_id_b])
    return fn
