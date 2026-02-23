"""Reward term factories for Isaac Lab environments.

Each factory returns a ``RewardTermCfg`` ready to drop into
``build_env_cfg(rewards={...})``.  Wraps Isaac Lab built-in MDP
functions and humanoid/locomotion-specific reward functions.
"""

from __future__ import annotations

from isaaclab.managers import RewardTermCfg as RewTerm, SceneEntityCfg

from isaaclab.envs.mdp import (
    action_l2 as _action_l2,
    action_rate_l2 as _action_rate_l2,
    ang_vel_xy_l2 as _ang_vel_xy_l2,
    flat_orientation_l2 as _flat_orientation_l2,
    is_alive as _is_alive,
    joint_vel_l2 as _joint_vel_l2,
    lin_vel_z_l2 as _lin_vel_z_l2,
)
from isaaclab_tasks.manager_based.classic.humanoid.mdp.rewards import (
    move_to_target_bonus as _move_to_target_bonus,
    joint_pos_limits_penalty_ratio as _joint_pos_limits_penalty_ratio,
    power_consumption as _power_consumption,
    progress_reward as _progress_reward,
    upright_posture_bonus as _upright_posture_bonus,
)


def progress_reward(
    weight: float = 1.0,
    target_pos: tuple = (1000.0, 0.0, 0.0),
) -> RewTerm:
    """Potential-based reward for moving towards a target position."""
    return RewTerm(func=_progress_reward, weight=weight,
                   params={"target_pos": target_pos})


def is_alive(weight: float = 0.5) -> RewTerm:
    """Constant bonus while the robot is not terminated."""
    return RewTerm(func=_is_alive, weight=weight)


def upright_posture(
    weight: float = 0.1,
    threshold: float = 0.93,
) -> RewTerm:
    """Bonus when the base up-projection exceeds *threshold*."""
    return RewTerm(func=_upright_posture_bonus, weight=weight,
                   params={"threshold": threshold})


def move_to_target(
    weight: float = 0.5,
    threshold: float = 0.8,
    target_pos: tuple = (1000.0, 0.0, 0.0),
) -> RewTerm:
    """Bonus for heading towards the target direction."""
    return RewTerm(func=_move_to_target_bonus, weight=weight,
                   params={"threshold": threshold, "target_pos": target_pos})


def action_l2(weight: float = -0.005) -> RewTerm:
    """L2 penalty on actions (use negative weight)."""
    return RewTerm(func=_action_l2, weight=weight)


def power_consumption(
    weight: float = -0.05,
    gear_ratio: dict | None = None,
) -> RewTerm:
    """Penalty for torque * velocity (energy usage)."""
    return RewTerm(func=_power_consumption, weight=weight,
                   params={"gear_ratio": gear_ratio or {".*": 15.0}})


def joint_pos_limits(
    weight: float = -0.1,
    threshold: float = 0.99,
    gear_ratio: dict | None = None,
) -> RewTerm:
    """Penalty when joints approach their limits."""
    return RewTerm(func=_joint_pos_limits_penalty_ratio, weight=weight,
                   params={"threshold": threshold,
                           "gear_ratio": gear_ratio or {".*": 15.0}})


def lin_vel_z_l2(weight: float = -0.5) -> RewTerm:
    """Penalty on vertical (z) base velocity -- reduces bouncing/jumping."""
    return RewTerm(func=_lin_vel_z_l2, weight=weight)


def ang_vel_xy_l2(weight: float = -0.05) -> RewTerm:
    """Penalty on roll/pitch angular velocity -- reduces tumbling."""
    return RewTerm(func=_ang_vel_xy_l2, weight=weight)


def flat_orientation_l2(weight: float = -0.1) -> RewTerm:
    """Penalty on non-flat orientation -- keeps the base level."""
    return RewTerm(func=_flat_orientation_l2, weight=weight)


def joint_vel_l2(weight: float = -0.001) -> RewTerm:
    """Penalty on joint velocities -- encourages smoother motion."""
    return RewTerm(func=_joint_vel_l2, weight=weight)


def action_rate_l2(weight: float = -0.01) -> RewTerm:
    """Penalty on action changes between steps -- smooths commands."""
    return RewTerm(func=_action_rate_l2, weight=weight)
