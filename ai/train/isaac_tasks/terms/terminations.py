"""Termination term factories for Isaac Lab environments.

Each factory returns a ``TerminationTermCfg`` ready to drop into
``build_env_cfg(terminations={...})``.
"""

from __future__ import annotations

from isaaclab.managers import TerminationTermCfg as DoneTerm

from isaaclab.envs.mdp import (
    root_height_below_minimum as _root_height_below_minimum,
    time_out as _time_out,
)


def time_out() -> DoneTerm:
    """Episode truncation when ``episode_length_s`` is exceeded."""
    return DoneTerm(func=_time_out, time_out=True)


def root_height_below(minimum_height: float = 0.31) -> DoneTerm:
    """Terminate when the robot root drops below *minimum_height*."""
    return DoneTerm(func=_root_height_below_minimum,
                    params={"minimum_height": minimum_height})
