"""MuJoCo simulation entry point (visualization modes only).

Usage (unified config -- launched from joshua_main or standalone):
    bazel run //launcher:joshua_main -- \
        --config config/config_preset/so_arm100_sim_interactive.pbtxt

    bazel run //simulation -- \
        --config config/config_preset/so_arm100_sim_mirror.pbtxt --mode interactive

Legacy standalone SimulationConfig pbtxt files are also accepted.
Training workflows (RL, imitation, eval) use ai/train/trainer.py instead.
"""

import sys
from typing import Optional, Tuple

import gflags
import glog
from google.protobuf import text_format

from config.proto import config_pb2
from simulation.modes import interactive, mirror, offscreen, passive
from simulation.mujoco_engine import MuJoCoEngine
from simulation.proto import simulation_pb2

FLAGS = gflags.FLAGS

gflags.DEFINE_string(
    "config", None, "Path to a Config or SimulationConfig .pbtxt file."
)
gflags.DEFINE_string(
    "mode",
    None,
    "Override the simulation sub-mode. "
    "One of: interactive, passive, mirror, offscreen.",
)

_MODE_ENUM = {
    "interactive": simulation_pb2.MODE_INTERACTIVE,
    "passive": simulation_pb2.MODE_PASSIVE,
    "mirror": simulation_pb2.MODE_MIRROR,
    "offscreen": simulation_pb2.MODE_OFFSCREEN,
}


def _load_config(
    path: str,
) -> Tuple[simulation_pb2.SimulationConfig, Optional[config_pb2.Config]]:
    """Load config, returning (SimulationConfig, full_config_or_None)."""
    with open(path) as f:
        raw = f.read()

    try:
        full = config_pb2.Config()
        text_format.Parse(raw, full)
        if full.HasField("simulation"):
            return full.simulation, full
    except text_format.ParseError:
        pass

    sim = simulation_pb2.SimulationConfig()
    text_format.Parse(raw, sim)
    return sim, None


def _run_mode(config: simulation_pb2.SimulationConfig, engine: MuJoCoEngine) -> None:
    mode = config.mode
    if mode == simulation_pb2.MODE_INTERACTIVE:
        interactive.run(engine)
    elif mode == simulation_pb2.MODE_PASSIVE:
        passive.run(engine, config.passive)
    elif mode == simulation_pb2.MODE_MIRROR:
        mirror.run(engine, config.mirror)
    elif mode == simulation_pb2.MODE_OFFSCREEN:
        offscreen.run(engine, config.offscreen)
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

    config, _ = _load_config(FLAGS.config)

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

    engine = MuJoCoEngine(config)
    try:
        _run_mode(config, engine)
    finally:
        engine.close()


if __name__ == "__main__":
    main(sys.argv)
