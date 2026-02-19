"""Interactive simulation mode.

Opens the MuJoCo GUI where the user can drag joints, orbit the camera,
and apply forces directly.
"""

from __future__ import annotations

import mujoco.viewer

from simulation.modes.mode_interface import SimulationMode
from simulation.sim_engine import SimEngine


class InteractiveMode(SimulationMode):

    def run(self, engine: SimEngine) -> None:
        mujoco.viewer.launch(engine.model, engine.data)
