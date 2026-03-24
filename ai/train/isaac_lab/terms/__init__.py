"""Shared term library for composing Isaac Lab environments.

Convenience factories that return Isaac Lab ``TermCfg`` objects.
Use with ``build_env_cfg()`` to compose environments declaratively.

Usage::

    from isaac_lab import terms

    rewards = {
        "progress": terms.progress_reward(weight=1.0),
        "alive":    terms.is_alive(weight=0.5),
        "action_l2": terms.action_l2(weight=-0.005),
    }
"""

from .actions import joint_effort_actions
from .events import reset_joints_by_offset, reset_root_state
from .observations import (
    base_ang_vel,
    base_angle_to_target,
    base_heading_proj,
    base_lin_vel,
    base_pos_z,
    base_up_proj,
    base_yaw_roll,
    body_forces,
    joint_pos_normalized,
    joint_vel_rel,
    last_action,
)
from .rewards import (
    action_l2,
    action_rate_l2,
    ang_vel_xy_l2,
    flat_orientation_l2,
    is_alive,
    joint_pos_limits,
    joint_vel_l2,
    lin_vel_z_l2,
    move_to_target,
    power_consumption,
    progress_reward,
    upright_posture,
)
from .terminations import root_height_below, time_out
