"""Ant locomotion env config: walk forward (+x direction).

Standard Ant-v4 reward decomposed into composable terms:
  - forward_velocity  (weight +1.0)
  - healthy_bonus      (weight +1.0)
  - action_l2          (weight -0.5)
"""

from __future__ import annotations

import mujoco

from ai.train.mjx_envs.env import MJXEnv, MJXEnvCfg, RewardTerm
from ai.train.mjx_envs.terms import observations, resets, rewards, terminations


def Env(model_path: str, frame_skip: int = 5, **kwargs) -> MJXEnv:
    """Factory that builds a fully-configured ant MJXEnv."""
    mj_model = mujoco.MjModel.from_xml_path(model_path)
    dt = mj_model.opt.timestep * frame_skip

    cfg = MJXEnvCfg(
        frame_skip=frame_skip,
        max_episode_steps=kwargs.get("max_episode_steps", 1000),
        rewards=[
            RewardTerm(rewards.forward_velocity(dt), weight=1.0),
            RewardTerm(rewards.healthy_bonus(z_low=0.2, z_high=1.0, bonus=1.0), weight=1.0),
            RewardTerm(rewards.action_l2(), weight=-0.5),
        ],
        observations=[
            observations.qpos(skip=2),
            observations.qvel(),
        ],
        terminations=[
            terminations.height_out_of_range(z_low=0.2, z_high=1.0),
        ],
        reset_fn=resets.locomotion_reset(
            initial_height=0.75,
            joint_noise=0.1,
            velocity_noise=0.1,
        ),
    )
    return MJXEnv(cfg, mj_model)
