"""MuJoCo simulation viewer entry point.

Usage:
    bazel run //simulation:viewer -- --config simulation/configs/so_arm100_interactive.pbtxt
    bazel run //simulation:viewer -- --config simulation/configs/so_arm100_mirror.pbtxt
    bazel run //simulation:viewer -- --config simulation/configs/so_arm100_passive.pbtxt

    # Override mode from config:
    bazel run //simulation:viewer -- --config simulation/configs/so_arm100_mirror.pbtxt \
        --mode interactive
"""

import sys

import gflags
import glog
from google.protobuf import text_format

from simulation.modes import gym, interactive, mirror, offscreen, passive
from simulation.proto import simulation_pb2
from simulation.sim_engine import SimEngine

FLAGS = gflags.FLAGS

gflags.DEFINE_string("config", None, "Path to a SimulationConfig .pbtxt file.")
gflags.DEFINE_string(
    "mode",
    None,
    "Override the mode in the config. "
    "One of: interactive, passive, mirror, offscreen, gym.",
)

_MODE_ENUM = {
    "interactive": simulation_pb2.MODE_INTERACTIVE,
    "passive": simulation_pb2.MODE_PASSIVE,
    "mirror": simulation_pb2.MODE_MIRROR,
    "offscreen": simulation_pb2.MODE_OFFSCREEN,
    "gym": simulation_pb2.MODE_GYM,
}


def _load_config(path: str) -> simulation_pb2.SimulationConfig:
    config = simulation_pb2.SimulationConfig()
    with open(path) as f:
        text_format.Parse(f.read(), config)
    return config


def _run_mode(config: simulation_pb2.SimulationConfig, engine: SimEngine) -> None:
    mode = config.mode
    if mode == simulation_pb2.MODE_INTERACTIVE:
        interactive.run(engine)
    elif mode == simulation_pb2.MODE_PASSIVE:
        passive.run(engine, config.passive)
    elif mode == simulation_pb2.MODE_MIRROR:
        mirror.run(engine, config.mirror)
    elif mode == simulation_pb2.MODE_OFFSCREEN:
        offscreen.run(engine, config.offscreen)
    elif mode == simulation_pb2.MODE_GYM:
        gym.run(engine, config.gym)
    else:
        raise ValueError(f"Unknown or unset simulation mode: {mode}")


def main(argv):
    try:
        argv = FLAGS(argv)
    except gflags.FlagsError as e:
        print(f"{e}\nUsage: {sys.argv[0]} ARGS\n{FLAGS}", file=sys.stderr)
        sys.exit(1)

    if not FLAGS.config:
        print("Error: --config is required.", file=sys.stderr)
        sys.exit(1)

    config = _load_config(FLAGS.config)

    if FLAGS.mode:
        override = FLAGS.mode.lower()
        if override not in _MODE_ENUM:
            print(
                f"Error: unknown mode '{override}'. "
                f"Choose from: {', '.join(_MODE_ENUM)}",
                file=sys.stderr,
            )
            sys.exit(1)
        config.mode = _MODE_ENUM[override]

    glog.info(f"Config: {FLAGS.config}")
    glog.info(f"  model: {config.model_path}")
    glog.info(f"  mode:  {simulation_pb2.SimulationMode.Name(config.mode)}")

    engine = SimEngine(config)
    try:
        _run_mode(config, engine)
    finally:
        engine.close()


if __name__ == "__main__":
    main(sys.argv)
