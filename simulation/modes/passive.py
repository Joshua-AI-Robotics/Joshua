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

from simulation.mujoco_engine import MuJoCoEngine
from simulation.proto import simulation_pb2


def _generate_demo_trajectory(
    num_actuators: int, num_steps: int = 2_000, amplitude: float = 0.35
) -> np.ndarray:
    """Generate a simple alternating gait for quick viewer demos."""
    if num_actuators <= 0:
        raise ValueError("Model has no actuators to drive.")

    t = np.linspace(0.0, 8.0 * np.pi, num_steps, dtype=np.float32)
    trajectory = np.zeros((num_steps, num_actuators), dtype=np.float32)

    for actuator_idx in range(num_actuators):
        phase = 0.0 if actuator_idx % 2 == 0 else np.pi
        # Keep neighboring joints out of phase so the body visibly rocks forward.
        trajectory[:, actuator_idx] = amplitude * np.sin(t + phase)

    return trajectory


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


def run(engine: MuJoCoEngine, config: simulation_pb2.PassiveConfig) -> None:
    speed = config.speed if config.speed > 0 else 1.0
    if config.trajectory_path:
        trajectory = load_trajectory(config.trajectory_path)
        glog.info(
            f"  trajectory: {trajectory.shape[0]} steps x "
            f"{trajectory.shape[1]} actuators"
        )
    else:
        trajectory = _generate_demo_trajectory(engine.num_actuators)
        glog.info(
            "  trajectory: generated built-in sinusoidal demo "
            f"({trajectory.shape[0]} steps x {trajectory.shape[1]} actuators)"
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
