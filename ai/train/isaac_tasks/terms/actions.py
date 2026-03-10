"""Action config factories for Isaac Lab environments.

Returns ``@configclass`` instances ready for ``build_env_cfg(actions_cfg=...)``.
"""

from __future__ import annotations

from isaaclab.envs.mdp import JointEffortActionCfg
from isaaclab.utils import configclass


def joint_effort_actions(
    scale: float = 7.5,
    joint_names: list[str] | None = None,
    asset_name: str = "robot",
):
    """Build an actions config for joint-effort control.

    Returns an **instance** of a dynamically created ``ActionsCfg``.
    """
    term = JointEffortActionCfg(
        asset_name=asset_name,
        joint_names=joint_names or [".*"],
        scale=scale,
    )
    ns = {
        "joint_effort": term,
        "__annotations__": {"joint_effort": type(term)},
    }
    ActionsCfg = configclass(type("ActionsCfg", (), ns))
    return ActionsCfg()
