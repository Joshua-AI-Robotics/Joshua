"""Three-legged locomotion -- Joshua task definition.

A simple tripod robot: one spherical torso with three capsule legs,
each connected by a single revolute hip joint (3 DOF total).

Each hip swings its leg up/down (horizontal rotation axis perpendicular
to the leg direction).  The robot must learn to coordinate three legs
to walk forward (+X direction).

The robot USD is stored at ``simulation/models/trileg_isaac.usda``.
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

_TRILEG_USD = resolve_model_path("trileg_isaac.usda")

_TRILEG_ROBOT = build_robot_from_usd(
    usd_path=_TRILEG_USD,
    init_pos=(0.0, 0.0, 0.45),
    init_joint_pos={"hip_.*": 0.9},
    actuator_stiffness=0.0,
    actuator_damping=5.0,
)


# -- Environment config -------------------------------------------------------

TrilegEnvCfg = build_env_cfg(
    scene_cfg=build_scene_cfg(
        robot=_TRILEG_ROBOT,
        num_envs=4096,
        env_spacing=4.0,
    ),
    actions_cfg=terms.joint_effort_actions(scale=5.0),
    rewards={
        # -- locomotion --
        "progress": terms.progress_reward(weight=1.0),
        "alive": terms.is_alive(weight=0.5),
        "upright": terms.upright_posture(weight=0.1, threshold=0.93),
        "move_to_target": terms.move_to_target(weight=0.5, threshold=0.8),
        # -- smoothness penalties (prevent jumping / chaotic motion) --
        "lin_vel_z": terms.lin_vel_z_l2(weight=-0.5),
        "ang_vel_xy": terms.ang_vel_xy_l2(weight=-0.05),
        "flat_orientation": terms.flat_orientation_l2(weight=-0.2),
        "action_rate": terms.action_rate_l2(weight=-0.01),
        "joint_vel": terms.joint_vel_l2(weight=-0.0005),
        # -- efficiency penalties --
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
        "leg_body_forces": terms.body_forces(
            body_names=["leg_a", "leg_b", "leg_c"],
            scale=0.1,
        ),
        "actions": terms.last_action(),
    },
    terminations={
        "time_out": terms.time_out(),
        "torso_height": terms.root_height_below(minimum_height=0.15),
    },
    events={
        "reset_base": terms.reset_root_state(),
        "reset_robot_joints": terms.reset_joints_by_offset(
            pos_range=(-0.1, 0.1), vel_range=(-0.05, 0.05),
        ),
    },
    decimation=2,
    episode_length_s=16.0,
    sim_dt=1.0 / 120.0,
)


# -- Agent config --------------------------------------------------------------

TrilegAgentCfg = build_rsl_rl_cfg(
    max_iterations=1500,
    num_steps_per_env=32,
    save_interval=50,
    experiment_name="trileg",
    actor_hidden_dims=[256, 128, 64],
    critic_hidden_dims=[256, 128, 64],
    learning_rate=3e-4,
    entropy_coef=0.01,
)


# -- Gym registration ----------------------------------------------------------

gym.register(
    id="Joshua-Trileg-v0",
    entry_point="isaaclab.envs:ManagerBasedRLEnv",
    disable_env_checker=True,
    kwargs={
        "env_cfg_entry_point": f"{__name__}:TrilegEnvCfg",
        "rsl_rl_cfg_entry_point": f"{__name__}:TrilegAgentCfg",
    },
)
