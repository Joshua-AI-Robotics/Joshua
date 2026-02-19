"""General-purpose MuJoCo viewer.

Usage (Bazel):
    bazel run //simulation:viewer -- --model simulation/models/so_arm100_scene.xml
    bazel run //simulation:viewer -- --model simulation/models/so_arm100_scene.xml --passive \
        --trajectory simulation/trajectories/example_trajectory.csv
    bazel run //simulation:viewer -- --model simulation/models/so_arm100_scene.xml --passive \
        --trajectory simulation/trajectories/example_trajectory.csv --speed 2.0
"""

import csv
import sys

import gflags
import glog
import mujoco
import mujoco.viewer
import numpy as np

FLAGS = gflags.FLAGS

gflags.DEFINE_string("model", None, "Path to MJCF (.xml) or URDF model file.")
gflags.DEFINE_bool("passive", False,
                   "Run passive viewer with trajectory playback instead of "
                   "interactive mode.")
gflags.DEFINE_string("trajectory", None,
                     "Path to trajectory file (.npy or .csv) for passive mode. "
                     "Rows are timesteps, columns are actuator controls.")
gflags.DEFINE_float("speed", 1.0, "Playback speed multiplier for passive mode.")


def _load_model(path: str) -> tuple[mujoco.MjModel, mujoco.MjData]:
    model = mujoco.MjModel.from_xml_path(path)
    data = mujoco.MjData(model)
    glog.info(f"Loaded: {path}")
    glog.info(f"  bodies={model.nbody}  joints={model.njnt}  "
              f"actuators={model.nu}  sensors={model.nsensor}")
    return model, data


def _load_trajectory(path: str) -> np.ndarray:
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


def run_interactive() -> None:
    model, data = _load_model(FLAGS.model)
    mujoco.viewer.launch(model, data)


def run_passive() -> None:
    if not FLAGS.trajectory:
        raise ValueError("--trajectory is required when --passive is set.")

    model, data = _load_model(FLAGS.model)
    trajectory = _load_trajectory(FLAGS.trajectory)
    glog.info(f"  trajectory: {trajectory.shape[0]} steps x "
              f"{trajectory.shape[1]} actuators")
    if trajectory.shape[1] != model.nu:
        glog.warning(f"trajectory has {trajectory.shape[1]} cols "
                     f"but model has {model.nu} actuators; clamping to min")

    max_steps = trajectory.shape[0]
    num_ctrl = min(trajectory.shape[1], model.nu)
    step = 0

    with mujoco.viewer.launch_passive(model, data) as viewer:
        while viewer.is_running():
            if step < max_steps:
                data.ctrl[:num_ctrl] = trajectory[step, :num_ctrl]
                step = min(step + int(FLAGS.speed), max_steps)

            mujoco.mj_step(model, data)
            viewer.sync()


def main(argv):
    try:
        argv = FLAGS(argv)
    except gflags.FlagsError as e:
        print(f"{e}\nUsage: {sys.argv[0]} ARGS\n{FLAGS}", file=sys.stderr)
        sys.exit(1)

    if not FLAGS.model:
        print("Error: --model is required.", file=sys.stderr)
        sys.exit(1)

    if FLAGS.passive:
        run_passive()
    else:
        run_interactive()


if __name__ == "__main__":
    main(sys.argv)
