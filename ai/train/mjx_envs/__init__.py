"""MJX environment package.

Each task lives in its own module (e.g. ``reach.py``, ``ant.py``) and
exports a class named ``Env``.  Use ``load_env`` to instantiate by task
name -- the module is imported dynamically so no central registry is
needed.
"""

from __future__ import annotations

import importlib

from ai.train.mjx_envs.base_env import EnvState, MJXBaseEnv, StepResult

__all__ = ["EnvState", "StepResult", "MJXBaseEnv", "load_env"]


def load_env(
    task: str,
    model_path: str,
    frame_skip: int = 10,
    **kwargs,
) -> MJXBaseEnv:
    """Instantiate an MJX env by task name.

    The *task* string maps 1-to-1 to a Python module inside this package
    (e.g. ``task="reach"`` loads ``ai.train.mjx_envs.reach``).  Each
    module must export a class named ``Env`` that subclasses
    ``MJXBaseEnv``.
    """
    try:
        mod = importlib.import_module(f"ai.train.mjx_envs.{task}")
    except ModuleNotFoundError as exc:
        raise ValueError(
            f"No MJX env module for task '{task}'. "
            f"Expected ai/train/mjx_envs/{task}.py"
        ) from exc

    env_cls = getattr(mod, "Env", None)
    if env_cls is None:
        raise AttributeError(
            f"Module ai.train.mjx_envs.{task} does not export an 'Env' class"
        )

    return env_cls(model_path=model_path, frame_skip=frame_skip, **kwargs)
