"""Replay an exported Ant trajectory in MuJoCo."""

from __future__ import annotations

import argparse
import csv
import time

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
        "--action_scale",
        type=float,
        default=None,
        help=(
            "Optional divisor applied to trajectory actions before clamping to "
            "the model ctrlrange. Useful if actions were exported in a scaled "
            "torque space (e.g. ~7.5x)."
        ),
    )
    parser.add_argument("--loop", action="store_true", help="Loop trajectory forever.")
    parser.add_argument(
        "--reset_on_loop",
        action="store_true",
        help="When looping, reset the simulator state back to the model default.",
    )
    parser.add_argument(
        "--reset_on_fall",
        action="store_true",
        help="Auto-reset if the base height drops below --fall_height.",
    )
    parser.add_argument(
        "--fall_height",
        type=float,
        default=0.20,
        help="Height threshold (meters) for --reset_on_fall.",
    )
    parser.add_argument(
        "--steps_per_frame",
        type=int,
        default=1,
        help="How many MuJoCo simulation steps to run per rendered frame.",
    )
    parser.add_argument(
        "--headless",
        action="store_true",
        help="Run without opening a viewer window (useful for CI / quick checks).",
    )
    parser.add_argument(
        "--realtime",
        action="store_true",
        help="Sleep to approximate real-time in viewer mode.",
    )
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
    # "speed" means trajectory-steps per rendered frame.
    # Allow fractional values (e.g. 0.25) to slow playback down.
    speed = float(args.speed) if args.speed > 0 else 1.0

    model = mujoco.MjModel.from_xml_path(args.model_path)
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)

    def reset_state() -> None:
        mujoco.mj_resetData(model, data)
        mujoco.mj_forward(model, data)

    trajectory = load_trajectory(args.trajectory_csv)
    if args.action_scale and args.action_scale > 0:
        trajectory = trajectory / float(args.action_scale)
        glog.info("applied action_scale=%s", args.action_scale)

    max_abs = float(np.max(np.abs(trajectory)))
    if max_abs > 1.0 + 1e-6:
        glog.warning(
            "trajectory max |action|=%s exceeds ctrlrange [-1,1]; values will be clamped. "
            "If this trajectory is in a scaled action space, try --action_scale (e.g. 7.5).",
            max_abs,
        )
    trajectory = np.clip(trajectory, -1.0, 1.0)

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
    step_f = 0.0

    if args.headless:
        # Step until we've applied the whole trajectory. Fractional speeds
        # just mean it takes more frames to finish.
        max_frames = int(np.ceil(max_steps / speed)) + 1
        for _ in range(max_frames):
            if step_f < max_steps:
                step = int(step_f)
                data.ctrl[:num_ctrl] = trajectory[step, :num_ctrl]
                step_f = min(step_f + speed, float(max_steps))
            mujoco.mj_step(model, data)
        glog.info("headless replay complete (step_f=%s).", step_f)
        glog.info("final qpos[0:3]=%s", data.qpos[:3])
        return

    with mujoco.viewer.launch_passive(model, data) as viewer:
        while viewer.is_running():
            if args.reset_on_fall and float(data.qpos[2]) < float(args.fall_height):
                reset_state()
                step_f = 0.0

            if step_f >= max_steps:
                if args.loop:
                    step_f = 0.0
                    if args.reset_on_loop:
                        reset_state()
                else:
                    step_f = float(max_steps)

            if step_f < max_steps:
                step = int(step_f)
                data.ctrl[:num_ctrl] = trajectory[step, :num_ctrl]
                step_f = min(step_f + speed, float(max_steps))

            for _ in range(max(1, int(args.steps_per_frame))):
                mujoco.mj_step(model, data)

            if args.realtime:
                time.sleep(float(model.opt.timestep) * max(1, int(args.steps_per_frame)))
            viewer.sync()


if __name__ == "__main__":
    main()
