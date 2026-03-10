"""Shared data types for MJX environments.

These NamedTuples are the JAX-friendly state / result containers used by
``MJXEnv``, the PPO training loop, and all term functions.
"""

from __future__ import annotations

from typing import NamedTuple

import jax
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
