"""Reach task -- move end-effector to a random target position."""

from __future__ import annotations

from typing import Dict

import mujoco
import numpy as np

_WORKSPACE_LOW = np.array([-0.15, -0.20, 0.01])
_WORKSPACE_HIGH = np.array([0.15, -0.05, 0.25])

_SUCCESS_THRESHOLD = 0.02  # metres

_EE_BODY = "Fixed_Jaw"

_TARGET_GEOM = "reach_target"


class ReachTask:
    """Dense reward for reaching a random 3-D target with the gripper."""

    def __init__(self) -> None:
        self._target_pos = np.zeros(3)
        self._rng = np.random.default_rng()

    def reset(self, model: mujoco.MjModel, data: mujoco.MjData) -> None:
        self._target_pos = self._rng.uniform(_WORKSPACE_LOW, _WORKSPACE_HIGH)

        geom_id = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_GEOM, _TARGET_GEOM)
        if geom_id >= 0:
            model.geom_pos[geom_id] = self._target_pos

    def compute_reward(self, obs: Dict[str, np.ndarray], action: np.ndarray) -> float:
        dist = float(np.linalg.norm(obs["ee_pos"] - obs["target_pos"]))
        return -dist

    def is_terminated(self, obs: Dict[str, np.ndarray]) -> bool:
        dist = float(np.linalg.norm(obs["ee_pos"] - obs["target_pos"]))
        return dist < _SUCCESS_THRESHOLD

    def get_task_obs(
        self, model: mujoco.MjModel, data: mujoco.MjData
    ) -> Dict[str, np.ndarray]:
        body_id = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_BODY, _EE_BODY)
        ee_pos = data.xpos[body_id].copy() if body_id >= 0 else np.zeros(3)
        return {
            "ee_pos": ee_pos,
            "target_pos": self._target_pos.copy(),
        }
