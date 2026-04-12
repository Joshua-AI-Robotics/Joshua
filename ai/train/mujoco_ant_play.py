"""Replay an exported Ant trajectory in MuJoCo."""

from __future__ import annotations

import argparse
import csv

import glog
import mujoco
import mujoco.viewer
import numpy as np



def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Replay an exported Ant action trajectory in MuJoCo."
    )
    parser.add_argument("--trajectory_csv", required=True)
    parser.add_argument("--speed", type=float, default=1.0)
    parser.add_argument(
        "--model_path",
        default="simulation/models/ant.xml",
        help="Path to the MuJoCo Ant model XML.",
    )
    return parser.parse_args()


def load_trajectory(path: str) -> np.ndarray:
    """Load a CSV trajectory with rows=timesteps and cols=actuators."""
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


def main() -> None:
    args = parse_args()
    speed = args.speed if args.speed > 0 else 1.0

    model = mujoco.MjModel.from_xml_path(args.model_path)
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)

    trajectory = load_trajectory(args.trajectory_csv)
    glog.info(
        "trajectory: %s steps x %s actuators",
        trajectory.shape[0],
        trajectory.shape[1],
    )

    if trajectory.shape[1] != model.nu:
        glog.warning(
            "trajectory has %s cols but model has %s actuators; clamping to min",
            trajectory.shape[1],
            model.nu,
        )

    max_steps = trajectory.shape[0]
    num_ctrl = min(trajectory.shape[1], model.nu)
    step = 0

    with mujoco.viewer.launch_passive(model, data) as viewer:
        while viewer.is_running():
            if step < max_steps:
                data.ctrl[:num_ctrl] = trajectory[step, :num_ctrl]
                step = min(step + int(speed), max_steps)

            mujoco.mj_step(model, data)
            viewer.sync()


if __name__ == "__main__":
    main()
