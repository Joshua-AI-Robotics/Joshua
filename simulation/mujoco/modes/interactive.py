"""Interactive simulation mode.

Opens the MuJoCo GUI where the user can drag joints, orbit the camera,
and apply forces directly.
"""

from __future__ import annotations

import mujoco.viewer

from simulation.mujoco.engine import MuJoCoEngine


def run(engine: MuJoCoEngine) -> None:
    mujoco.viewer.launch(engine.model, engine.data)
