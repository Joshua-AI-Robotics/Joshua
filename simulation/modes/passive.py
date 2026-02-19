"""Passive simulation mode -- trajectory playback.

Loads a trajectory file (.npy or .csv) and replays it through the
MuJoCo passive viewer.
"""

from __future__ import annotations

import csv

import glog
import mujoco
import mujoco.viewer
import numpy as np

from simulation.proto import simulation_pb2
from simulation.sim_engine import SimEngine


def load_trajectory(path: str) -> np.ndarray:
    """Load a trajectory file. Supports .npy and .csv (rows=timesteps, cols=actuators)."""
    if path.endswith(".npy"):
        return np.load(path)
    with open(path) as f:
        reader = csv.reader(f)
        rows = []
        for row in reader:
            try:
                rows.append([float(v) for v in row])
            except ValueError:
                continue
    if not rows:
        raise ValueError(f"No numeric data found in {path}")
    return np.array(rows)


def run(engine: SimEngine, config: simulation_pb2.PassiveConfig) -> None:
    if not config.trajectory_path:
        raise ValueError("PassiveConfig.trajectory_path is required for passive mode.")

    speed = config.speed if config.speed > 0 else 1.0
    trajectory = load_trajectory(config.trajectory_path)
    glog.info(
        f"  trajectory: {trajectory.shape[0]} steps x "
        f"{trajectory.shape[1]} actuators"
    )

    if trajectory.shape[1] != engine.num_actuators:
        glog.warning(
            f"trajectory has {trajectory.shape[1]} cols but model has "
            f"{engine.num_actuators} actuators; clamping to min"
        )

    max_steps = trajectory.shape[0]
    num_ctrl = min(trajectory.shape[1], engine.num_actuators)
    step = 0

    with mujoco.viewer.launch_passive(engine.model, engine.data) as viewer:
        while viewer.is_running():
            if step < max_steps:
                engine.data.ctrl[:num_ctrl] = trajectory[step, :num_ctrl]
                step = min(step + int(speed), max_steps)

            engine.step()
            viewer.sync()
