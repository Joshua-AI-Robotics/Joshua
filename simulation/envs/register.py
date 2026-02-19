"""Register Gymnasium environments for the SO-100 arm.

Import this module once to make the environment IDs available:

    import simulation.envs.register
    env = gymnasium.make("SO100Reach-v0")
"""

from __future__ import annotations

import os

import gymnasium

_MODEL_DIR = os.path.join(os.path.dirname(__file__), os.pardir, "models")


def _model_path(filename: str) -> str:
    return os.path.normpath(os.path.join(_MODEL_DIR, filename))


def _make_reach(**kwargs):
    from simulation.envs.base_env import MuJoCoEnv
    from simulation.envs.reach import ReachTask

    return MuJoCoEnv(
        model_path=_model_path("so_arm100_reach.xml"),
        task=ReachTask(),
        **kwargs,
    )


def _make_pick_place(**kwargs):
    from simulation.envs.base_env import MuJoCoEnv
    from simulation.envs.pick_place import PickPlaceTask

    return MuJoCoEnv(
        model_path=_model_path("so_arm100_pick_place.xml"),
        task=PickPlaceTask(),
        **kwargs,
    )


gymnasium.register(
    id="SO100Reach-v0",
    entry_point=_make_reach,
    max_episode_steps=500,
)

gymnasium.register(
    id="SO100PickPlace-v0",
    entry_point=_make_pick_place,
    max_episode_steps=1000,
)
