"""Offscreen simulation mode -- headless rendering.

Steps the simulation and saves rendered frames as images.
Useful for generating synthetic training data without a display.
"""

from __future__ import annotations

import os

import glog
import numpy as np
from PIL import Image

from simulation.modes.mode_interface import SimulationMode
from simulation.proto import simulation_pb2
from simulation.sim_engine import SimEngine


class OffscreenMode(SimulationMode):

    def __init__(self, config: simulation_pb2.OffscreenConfig) -> None:
        self._camera_name = config.camera_name or "front"
        self._width = config.width or 640
        self._height = config.height or 480
        self._output_dir = config.output_dir or "/tmp/mujoco_frames"
        self._num_steps = config.num_steps or 500

    def run(self, engine: SimEngine) -> None:
        os.makedirs(self._output_dir, exist_ok=True)
        glog.info(
            f"Offscreen mode: rendering {self._num_steps} frames "
            f"({self._width}x{self._height}) from camera '{self._camera_name}' "
            f"to {self._output_dir}"
        )

        for i in range(self._num_steps):
            engine.step()
            rgb = engine.render(
                width=self._width,
                height=self._height,
                camera=self._camera_name,
            )
            path = os.path.join(self._output_dir, f"frame_{i:06d}.png")
            Image.fromarray(rgb).save(path)

            if (i + 1) % 100 == 0:
                glog.info(f"  rendered {i + 1}/{self._num_steps} frames")

        glog.info(
            f"Done. {self._num_steps} frames saved to {self._output_dir}"
        )
