"""Pick-and-place env config: grasp object and move to target.

Reward: -(dist_ee_to_obj + dist_obj_to_target).
Terminates when object reaches target or on time-out.
"""

from __future__ import annotations

import mujoco

from ai.train.mjx_envs.env import MJXEnv, MJXEnvCfg, RewardTerm
from ai.train.mjx_envs.terms import observations, resets, rewards, terminations


def Env(model_path: str, frame_skip: int = 10, **kwargs) -> MJXEnv:
    """Factory that builds a fully-configured pick-place MJXEnv."""
    mj_model = mujoco.MjModel.from_xml_path(model_path)

    ee_body_id = mujoco.mj_name2id(
        mj_model, mujoco.mjtObj.mjOBJ_BODY, "Fixed_Jaw"
    )
    obj_body_id = mujoco.mj_name2id(
        mj_model, mujoco.mjtObj.mjOBJ_BODY, "pick_object"
    )
    obj_jnt_id = mujoco.mj_name2id(
        mj_model, mujoco.mjtObj.mjOBJ_JOINT, "object_joint"
    )
    obj_qpos_adr = int(mj_model.jnt_qposadr[obj_jnt_id])
    obj_dof_adr = int(mj_model.jnt_dofadr[obj_jnt_id])

    cfg = MJXEnvCfg(
        frame_skip=frame_skip,
        max_episode_steps=kwargs.get("max_episode_steps", 1000),
        rewards=[
            RewardTerm(
                rewards.body_body_distance(ee_body_id, obj_body_id), weight=-1.0
            ),
            RewardTerm(
                rewards.body_target_distance(obj_body_id), weight=-1.0
            ),
        ],
        observations=[
            observations.qpos(),
            observations.qvel(),
            observations.body_position(ee_body_id),
            observations.body_position(obj_body_id),
            observations.target_position(),
        ],
        terminations=[
            terminations.object_target_reached(obj_body_id, threshold=0.03),
        ],
        reset_fn=resets.pick_place_reset(
            obj_qpos_adr=obj_qpos_adr,
            obj_dof_adr=obj_dof_adr,
            obj_spawn_low=[0.02, -0.18, 0.02],
            obj_spawn_high=[0.12, -0.10, 0.02],
            tgt_spawn_low=[-0.10, -0.18, 0.02],
            tgt_spawn_high=[0.10, -0.08, 0.02],
        ),
    )
    return MJXEnv(cfg, mj_model)
