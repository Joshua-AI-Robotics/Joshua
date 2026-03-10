"""Event term factories for Isaac Lab environments.

Each factory returns an ``EventTermCfg`` ready to drop into
``build_env_cfg(events={...})``.
"""

from __future__ import annotations

from isaaclab.managers import EventTermCfg as EventTerm

from isaaclab.envs.mdp import (
    reset_joints_by_offset as _reset_joints_by_offset,
    reset_root_state_uniform as _reset_root_state_uniform,
)


def reset_root_state(
    pose_range: dict | None = None,
    velocity_range: dict | None = None,
) -> EventTerm:
    """Reset root state with optional uniform randomization."""
    return EventTerm(
        func=_reset_root_state_uniform,
        mode="reset",
        params={
            "pose_range": pose_range or {},
            "velocity_range": velocity_range or {},
        },
    )


def reset_joints_by_offset(
    pos_range: tuple = (-0.2, 0.2),
    vel_range: tuple = (-0.1, 0.1),
) -> EventTerm:
    """Reset joints with random offsets around the default pose."""
    return EventTerm(
        func=_reset_joints_by_offset,
        mode="reset",
        params={
            "position_range": pos_range,
            "velocity_range": vel_range,
        },
    )
