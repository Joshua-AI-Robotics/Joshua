"""Base types and class for MJX (MuJoCo XLA) environments.

All MJX envs share the same state/result types and common initialization
logic (model loading, action bounds, physics stepping). Task-specific
envs subclass MJXBaseEnv and live in their own modules (one env per file).
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


class MJXBaseEnv:
    """Common setup shared by all MJX task environments.

    Subclasses must set ``self.obs_size`` and implement ``reset`` / ``step``.
    """

    def __init__(
        self,
        model_path: str,
        frame_skip: int = 10,
        max_episode_steps: int = 500,
    ) -> None:
        self.mj_model = mujoco.MjModel.from_xml_path(model_path)
        self.mjx_model = mjx.put_model(self.mj_model)
        self._frame_skip = frame_skip
        self._max_steps = max_episode_steps
        self._dt = self.mj_model.opt.timestep * frame_skip
        self._init_data = mjx.make_data(self.mjx_model)

        self.action_size: int = self.mj_model.nu
        self.action_low = jnp.array(self.mj_model.actuator_ctrlrange[:, 0])
        self.action_high = jnp.array(self.mj_model.actuator_ctrlrange[:, 1])

        self.obs_size: int = 0  # subclass must override

    def _physics_step(self, data: mjx.Data) -> mjx.Data:
        """Run frame_skip sub-steps of MJX physics."""
        def _step(d, _):
            return mjx.step(self.mjx_model, d), None

        data, _ = jax.lax.scan(_step, data, None, length=self._frame_skip)
        return data

    def reset(self, rng: jax.Array) -> Tuple[EnvState, jax.Array]:
        raise NotImplementedError

    def step(self, state: EnvState, action: jax.Array) -> StepResult:
        raise NotImplementedError
