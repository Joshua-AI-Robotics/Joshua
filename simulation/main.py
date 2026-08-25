"""Simulation entry point (MuJoCo and Isaac Sim backends).

Usage (unified config -- launched from joshua_main or standalone):
    bazel run //launcher:joshua_main -- \
        --config config/config_preset/so100/sim_interactive.pbtxt

    bazel run //simulation:simulation -- \
        --config config/config_preset/so100/sim_mirror.pbtxt --mode interactive

    # Isaac Sim viewer needs a preset with sim_backend: SIM_BACKEND_ISAAC_SIM;
    # none ships with the repo. See simulation/README.md.

Legacy standalone SimulationConfig pbtxt files are also accepted.
"""

import os
import sys

# Python prepends the script's own directory (simulation/) to sys.path,
# which makes the local mujoco/ backend package shadow the pip-installed
# mujoco engine. Drop that entry -- every import in this project is
# absolute from the workspace root. realpath() is needed because under
# Bazel the script is a symlink into the source tree.
_SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
sys.path[:] = [p for p in sys.path if os.path.realpath(p or os.getcwd()) != _SCRIPT_DIR]

from typing import Optional, Tuple  # noqa: E402

import gflags  # noqa: E402
import glog  # noqa: E402
from google.protobuf import text_format  # noqa: E402

from config.proto import config_pb2  # noqa: E402
from simulation.mujoco.engine import MuJoCoEngine  # noqa: E402
from simulation.mujoco.modes import (  # noqa: E402
    interactive,
    mirror,
    offscreen,
    passive,
)
from simulation.proto import simulation_pb2  # noqa: E402

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

    if config.backend == simulation_pb2.SIM_BACKEND_ISAAC_SIM:
        from simulation.isaac import launcher as isaac_launcher

        glog.info(f"Config: {FLAGS.config}")
        glog.info(f"  backend: ISAAC_SIM, usd: {config.isaac.usd_filename}")
        sys.exit(isaac_launcher.launch(config.isaac))

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
