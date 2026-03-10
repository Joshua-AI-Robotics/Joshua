"""Reach env config: move end-effector to a random 3D target.

Reward: negative distance from end-effector to target.
Terminates on success (distance < threshold) or time-out.
"""

from __future__ import annotations

import mujoco

from ai.train.mjx_envs.env import MJXEnv, MJXEnvCfg, RewardTerm
from ai.train.mjx_envs.terms import observations, resets, rewards, terminations


def Env(model_path: str, frame_skip: int = 10, **kwargs) -> MJXEnv:
    """Factory that builds a fully-configured reach MJXEnv."""
    mj_model = mujoco.MjModel.from_xml_path(model_path)

    ee_body_id = mujoco.mj_name2id(
        mj_model, mujoco.mjtObj.mjOBJ_BODY, "Fixed_Jaw"
    )

    cfg = MJXEnvCfg(
        frame_skip=frame_skip,
        max_episode_steps=kwargs.get("max_episode_steps", 500),
        rewards=[
            RewardTerm(rewards.body_target_distance(ee_body_id), weight=-1.0),
        ],
        observations=[
            observations.qpos(),
            observations.qvel(),
            observations.body_position(ee_body_id),
            observations.target_position(),
        ],
        terminations=[
            terminations.body_target_reached(ee_body_id, threshold=0.02),
        ],
        reset_fn=resets.random_target_reset(
            ws_low=[-0.15, -0.20, 0.01],
            ws_high=[0.15, -0.05, 0.25],
        ),
    )
    return MJXEnv(cfg, mj_model)
