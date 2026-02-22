"""Ant locomotion -- Joshua task definition.

Thin composition file: selects robot, terms, and agent config.
Mirrors ``ai/train/mjx_envs/ant.py``.

The robot USD is stored locally at ``simulation/models/ant_isaac.usda``.
"""

from __future__ import annotations

import gymnasium as gym

from isaac_tasks import terms
from isaac_tasks.agents import build_rsl_rl_cfg
from isaac_tasks.env_builder import (
    build_env_cfg,
    build_robot_from_usd,
    build_scene_cfg,
    resolve_model_path,
)

_ANT_USD = resolve_model_path("ant_isaac.usda")

_ANT_ROBOT = build_robot_from_usd(
    usd_path=_ANT_USD,
    init_pos=(0.0, 0.0, 0.5),
    init_joint_pos={
        ".*_leg": 0.0,
        "front_left_foot": 0.785398,
        "front_right_foot": -0.785398,
        "left_back_foot": -0.785398,
        "right_back_foot": 0.785398,
    },
)


# ── Environment config (class) ───────────────────────────────────────

AntEnvCfg = build_env_cfg(
    scene_cfg=build_scene_cfg(
        robot=_ANT_ROBOT,
        num_envs=4096,
        env_spacing=5.0,
    ),
    actions_cfg=terms.joint_effort_actions(scale=7.5),
    rewards={
        "progress": terms.progress_reward(weight=1.0),
        "alive": terms.is_alive(weight=0.5),
        "upright": terms.upright_posture(weight=0.1, threshold=0.93),
        "move_to_target": terms.move_to_target(weight=0.5, threshold=0.8),
        "action_l2": terms.action_l2(weight=-0.005),
        "energy": terms.power_consumption(weight=-0.05),
        "joint_pos_limits": terms.joint_pos_limits(weight=-0.1),
    },
    observations={
        "base_height": terms.base_pos_z(),
        "base_lin_vel": terms.base_lin_vel(),
        "base_ang_vel": terms.base_ang_vel(),
        "base_yaw_roll": terms.base_yaw_roll(),
        "base_angle_to_target": terms.base_angle_to_target(),
        "base_up_proj": terms.base_up_proj(),
        "base_heading_proj": terms.base_heading_proj(),
        "joint_pos_norm": terms.joint_pos_normalized(),
        "joint_vel_rel": terms.joint_vel_rel(scale=0.2),
        "feet_body_forces": terms.body_forces(
            body_names=[
                "front_left_foot",
                "front_right_foot",
                "left_back_foot",
                "right_back_foot",
            ],
            scale=0.1,
        ),
        "actions": terms.last_action(),
    },
    terminations={
        "time_out": terms.time_out(),
        "torso_height": terms.root_height_below(minimum_height=0.31),
    },
    events={
        "reset_base": terms.reset_root_state(),
        "reset_robot_joints": terms.reset_joints_by_offset(
            pos_range=(-0.2, 0.2), vel_range=(-0.1, 0.1),
        ),
    },
    decimation=2,
    episode_length_s=16.0,
    sim_dt=1.0 / 120.0,
)


# ── Agent config (class) ─────────────────────────────────────────────

AntAgentCfg = build_rsl_rl_cfg(
    max_iterations=1000,
    num_steps_per_env=32,
    save_interval=50,
    experiment_name="ant",
    actor_hidden_dims=[400, 200, 100],
    critic_hidden_dims=[400, 200, 100],
    learning_rate=5e-4,
    entropy_coef=0.0,
)


# ── Gym registration ─────────────────────────────────────────────────

gym.register(
    id="Joshua-Ant-v0",
    entry_point="isaaclab.envs:ManagerBasedRLEnv",
    disable_env_checker=True,
    kwargs={
        "env_cfg_entry_point": f"{__name__}:AntEnvCfg",
        "rsl_rl_cfg_entry_point": f"{__name__}:AntAgentCfg",
    },
)
