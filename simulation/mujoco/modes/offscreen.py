"""Offscreen simulation mode -- headless rendering.

Steps the simulation and saves rendered frames as images.
Useful for generating synthetic training data without a display.
"""

from __future__ import annotations

import os

import glog
from PIL import Image

from simulation.mujoco.engine import MuJoCoEngine
from simulation.proto import simulation_pb2


def run(engine: MuJoCoEngine, config: simulation_pb2.OffscreenConfig) -> None:
    camera_name = config.camera_name or "front"
    width = config.width or 640
    height = config.height or 480
    output_dir = config.output_dir or "/tmp/mujoco_frames"
    num_steps = config.num_steps or 500

    os.makedirs(output_dir, exist_ok=True)
    glog.info(
        f"Offscreen mode: rendering {num_steps} frames "
        f"({width}x{height}) from camera '{camera_name}' "
        f"to {output_dir}"
    )

    for i in range(num_steps):
        engine.step()
        rgb = engine.render(width=width, height=height, camera=camera_name)
        path = os.path.join(output_dir, f"frame_{i:06d}.png")
        Image.fromarray(rgb).save(path)

        if (i + 1) % 100 == 0:
            glog.info(f"  rendered {i + 1}/{num_steps} frames")

    glog.info(f"Done. {num_steps} frames saved to {output_dir}")
