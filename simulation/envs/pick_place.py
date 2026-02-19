"""Pick-and-place task -- grasp an object and move it to a target location."""

from __future__ import annotations

from typing import Dict

import mujoco
import numpy as np

_EE_BODY = "Fixed_Jaw"
_OBJECT_BODY = "pick_object"
_TARGET_BODY = "place_target"

_OBJECT_SPAWN_LOW = np.array([0.02, -0.18, 0.02])
_OBJECT_SPAWN_HIGH = np.array([0.12, -0.10, 0.02])

_TARGET_SPAWN_LOW = np.array([-0.10, -0.18, 0.02])
_TARGET_SPAWN_HIGH = np.array([0.10, -0.08, 0.02])

_SUCCESS_THRESHOLD = 0.03  # metres


class PickPlaceTask:
    """Two-phase reward: reach the object, then place it at the target."""

    def __init__(self) -> None:
        self._target_pos = np.zeros(3)
        self._rng = np.random.default_rng()

    def reset(self, model: mujoco.MjModel, data: mujoco.MjData) -> None:
        obj_pos = self._rng.uniform(_OBJECT_SPAWN_LOW, _OBJECT_SPAWN_HIGH)
        self._target_pos = self._rng.uniform(_TARGET_SPAWN_LOW, _TARGET_SPAWN_HIGH)

        obj_body_id = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_BODY, _OBJECT_BODY)
        obj_jnt_id = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_JOINT, "object_joint")
        if obj_jnt_id >= 0:
            qpos_adr = model.jnt_qposadr[obj_jnt_id]
            data.qpos[qpos_adr : qpos_adr + 3] = obj_pos
            data.qpos[qpos_adr + 3 : qpos_adr + 7] = [1, 0, 0, 0]
            data.qvel[
                model.jnt_dofadr[obj_jnt_id] : model.jnt_dofadr[obj_jnt_id] + 6
            ] = 0

        tgt_body_id = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_BODY, _TARGET_BODY)
        if tgt_body_id >= 0:
            data.mocap_pos[model.body_mocapid[tgt_body_id]] = self._target_pos

    def compute_reward(self, obs: Dict[str, np.ndarray], action: np.ndarray) -> float:
        ee_pos = obs["ee_pos"]
        obj_pos = obs["object_pos"]
        target_pos = obs["target_pos"]

        dist_ee_obj = float(np.linalg.norm(ee_pos - obj_pos))
        dist_obj_tgt = float(np.linalg.norm(obj_pos - target_pos))

        return -(dist_ee_obj + dist_obj_tgt)

    def is_terminated(self, obs: Dict[str, np.ndarray]) -> bool:
        dist_obj_tgt = float(np.linalg.norm(obs["object_pos"] - obs["target_pos"]))
        return dist_obj_tgt < _SUCCESS_THRESHOLD

    def get_task_obs(
        self, model: mujoco.MjModel, data: mujoco.MjData
    ) -> Dict[str, np.ndarray]:
        ee_id = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_BODY, _EE_BODY)
        ee_pos = data.xpos[ee_id].copy() if ee_id >= 0 else np.zeros(3)

        obj_id = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_BODY, _OBJECT_BODY)
        obj_pos = data.xpos[obj_id].copy() if obj_id >= 0 else np.zeros(3)

        return {
            "ee_pos": ee_pos,
            "object_pos": obj_pos,
            "target_pos": self._target_pos.copy(),
        }
