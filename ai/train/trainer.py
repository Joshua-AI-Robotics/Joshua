"""Training entry point.

Dispatches to the Isaac Sim backend and training method (RL, imitation,
eval) based on the unified Config.

Usage:
    bazel run //ai/train:trainer -- \
        --config config/config_preset/ant/ant_train_isaac_full_skrl.pbtxt
"""

import sys

import gflags
import glog
from google.protobuf import text_format

from ai.proto import training_pb2
from config.proto import config_pb2

FLAGS = gflags.FLAGS

gflags.DEFINE_string("config", None, "Path to a Config .pbtxt file.")


def _load_training_config(path: str) -> training_pb2.TrainingConfig:
    with open(path) as f:
        raw = f.read()

    full = config_pb2.Config()
    text_format.Parse(raw, full)
    return full.ai.training


def _run_rl(config: training_pb2.TrainingConfig) -> None:
    """Dispatch RL training to Isaac Sim."""
    if config.environment != training_pb2.TRAINING_ENV_SIMULATION:
        glog.warning("RL training currently only supports simulation environment")

    backend = config.simulator_backend

    if backend == training_pb2.SIM_BACKEND_ISAAC_SIM:
        from ai.train.isaac_launcher import launch_isaac_training

        launch_isaac_training(config)

    else:
        raise ValueError(
            f"Unknown or unset simulator_backend: {backend}. "
            f"Set simulator_backend: SIM_BACKEND_ISAAC_SIM in your config."
        )


def _run_eval(config: training_pb2.TrainingConfig) -> None:
    """Dispatch evaluation to Isaac Sim."""
    if config.environment != training_pb2.TRAINING_ENV_SIMULATION:
        raise NotImplementedError("Real-world evaluation is not yet implemented")

    backend = config.simulator_backend

    if backend == training_pb2.SIM_BACKEND_ISAAC_SIM:
        from ai.train.isaac_launcher import launch_isaac_eval

        launch_isaac_eval(config)

    else:
        raise ValueError(
            f"Unknown or unset simulator_backend: {backend}. "
            f"Set simulator_backend: SIM_BACKEND_ISAAC_SIM in your config."
        )


def _run_trajectory_export(config: training_pb2.TrainingConfig) -> None:
    """Dispatch trajectory export to Isaac Sim."""
    if config.environment != training_pb2.TRAINING_ENV_SIMULATION:
        raise NotImplementedError(
            "Trajectory export only supports simulation environment")

    backend = config.simulator_backend

    if backend == training_pb2.SIM_BACKEND_ISAAC_SIM:
        from ai.train.isaac_launcher import launch_isaac_trajectory_export

        launch_isaac_trajectory_export(config)

    else:
        raise ValueError(
            f"Unknown or unset simulator_backend: {backend}. "
            f"Set simulator_backend: SIM_BACKEND_ISAAC_SIM in your config."
        )


def main(argv):
    try:
        argv = FLAGS(argv)
    except gflags.FlagsError as e:
        print(f"{e}\nUsage: {sys.argv[0]} ARGS\n{FLAGS}", file=sys.stderr)
        sys.exit(1)

    if not FLAGS.config:
        print("Error: --config is required.", file=sys.stderr)
        sys.exit(1)

    config = _load_training_config(FLAGS.config)

    env_name = training_pb2.TrainingEnvironment.Name(config.environment)
    method_name = training_pb2.TrainingMethod.Name(config.method)
    backend_name = training_pb2.SimulatorBackend.Name(config.simulator_backend)
    glog.info(
        f"Training config: environment={env_name}, method={method_name}, "
        f"backend={backend_name}"
    )

    if config.method == training_pb2.TRAINING_METHOD_RL:
        _run_rl(config)

    elif config.method == training_pb2.TRAINING_METHOD_IMITATION:
        glog.info("Imitation learning — not yet implemented")
        glog.info(f"  dataset_path: {config.imitation.dataset_path}")
        glog.info(f"  base_model:   {config.imitation.base_model}")
        raise NotImplementedError("Imitation learning is not yet implemented")

    elif config.method == training_pb2.TRAINING_METHOD_EVAL:
        _run_eval(config)

    elif config.method == training_pb2.TRAINING_METHOD_TRAJECTORY_EXPORT:
        _run_trajectory_export(config)

    else:
        raise ValueError(f"Unknown training method: {config.method}")

    glog.info("Done.")


if __name__ == "__main__":
    main(sys.argv)
