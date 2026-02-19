"""Task protocol for Gymnasium environments.

Any task must satisfy this interface. No base class needed -- just implement
the four methods and pass an instance to MuJoCoEnv.
"""

from __future__ import annotations

from typing import Dict, Protocol, runtime_checkable

import mujoco
import numpy as np


@runtime_checkable
class Task(Protocol):

    def reset(self, model: mujoco.MjModel, data: mujoco.MjData) -> None:
        """Reset task state (e.g. randomise target position)."""
        ...

    def compute_reward(self, obs: Dict[str, np.ndarray], action: np.ndarray) -> float:
        """Return a scalar reward for the current transition."""
        ...

    def is_terminated(self, obs: Dict[str, np.ndarray]) -> bool:
        """Return True when the episode should end (success / failure)."""
        ...

    def get_task_obs(
        self, model: mujoco.MjModel, data: mujoco.MjData
    ) -> Dict[str, np.ndarray]:
        """Return task-specific observation entries to merge into the obs dict."""
        ...
