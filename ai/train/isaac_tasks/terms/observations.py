"""Observation term factories for Isaac Lab environments.

Each factory returns an ``ObservationTermCfg`` ready to drop into
``build_env_cfg(observations={...})``.
"""

from __future__ import annotations

from isaaclab.managers import ObservationTermCfg as ObsTerm, SceneEntityCfg

from isaaclab.envs.mdp import (
    base_ang_vel as _base_ang_vel,
    base_lin_vel as _base_lin_vel,
    base_pos_z as _base_pos_z,
    body_incoming_wrench as _body_incoming_wrench,
    joint_pos_limit_normalized as _joint_pos_limit_normalized,
    joint_vel_rel as _joint_vel_rel,
    last_action as _last_action,
)
from isaaclab_tasks.manager_based.classic.humanoid.mdp.observations import (
    base_angle_to_target as _base_angle_to_target,
    base_heading_proj as _base_heading_proj,
    base_up_proj as _base_up_proj,
    base_yaw_roll as _base_yaw_roll,
)


def base_pos_z() -> ObsTerm:
    """Height of the robot base."""
    return ObsTerm(func=_base_pos_z)


def base_lin_vel() -> ObsTerm:
    """Linear velocity of the robot base."""
    return ObsTerm(func=_base_lin_vel)


def base_ang_vel() -> ObsTerm:
    """Angular velocity of the robot base."""
    return ObsTerm(func=_base_ang_vel)


def base_yaw_roll() -> ObsTerm:
    """Yaw and roll angles of the base."""
    return ObsTerm(func=_base_yaw_roll)


def base_angle_to_target(target_pos: tuple = (1000.0, 0.0, 0.0)) -> ObsTerm:
    """Angle between base heading and target direction."""
    return ObsTerm(func=_base_angle_to_target,
                   params={"target_pos": target_pos})


def base_up_proj() -> ObsTerm:
    """Projection of base up vector onto world up."""
    return ObsTerm(func=_base_up_proj)


def base_heading_proj(target_pos: tuple = (1000.0, 0.0, 0.0)) -> ObsTerm:
    """Projection of base heading onto target direction."""
    return ObsTerm(func=_base_heading_proj,
                   params={"target_pos": target_pos})


def joint_pos_normalized() -> ObsTerm:
    """Joint positions normalized to their limits."""
    return ObsTerm(func=_joint_pos_limit_normalized)


def joint_vel_rel(scale: float = 0.2) -> ObsTerm:
    """Relative joint velocities, scaled."""
    return ObsTerm(func=_joint_vel_rel, scale=scale)


def body_forces(
    body_names: list[str],
    scale: float = 0.1,
    asset_name: str = "robot",
) -> ObsTerm:
    """Incoming wrench (forces/torques) on specified bodies."""
    return ObsTerm(
        func=_body_incoming_wrench,
        scale=scale,
        params={"asset_cfg": SceneEntityCfg(asset_name, body_names=body_names)},
    )


def last_action() -> ObsTerm:
    """Previous action applied to the environment."""
    return ObsTerm(func=_last_action)
