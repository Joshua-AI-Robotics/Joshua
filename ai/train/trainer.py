"""Training entry point.

Dispatches to MJX GPU-parallel RL, imitation learning, or evaluation
based on the TrainingConfig in the unified Config.

Usage:
    bazel run //ai/train:trainer -- \
        --config config/config_preset/ant_train_mjx.pbtxt
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
    glog.info(f"Training config: environment={env_name}, method={method_name}")

    if config.method == training_pb2.TRAINING_METHOD_RL:
        if config.environment != training_pb2.TRAINING_ENV_SIMULATION:
            glog.warning("RL training currently only supports simulation environment")

        from ai.train.mjx_rl import run as run_mjx
        run_mjx(config.rl)

    elif config.method == training_pb2.TRAINING_METHOD_IMITATION:
        glog.info("Imitation learning — not yet implemented")
        glog.info(f"  dataset_path: {config.imitation.dataset_path}")
        glog.info(f"  base_model:   {config.imitation.base_model}")
        raise NotImplementedError("Imitation learning is not yet implemented")

    elif config.method == training_pb2.TRAINING_METHOD_EVAL:
        if config.environment != training_pb2.TRAINING_ENV_SIMULATION:
            raise NotImplementedError("Real-world evaluation is not yet implemented")

        from ai.train.mjx_rl import eval_mjx

        rl_config = training_pb2.RLConfig(
            task=config.eval.task,
            checkpoint_path=config.eval.checkpoint_path,
            num_eval_episodes=config.eval.num_episodes,
            total_timesteps=0,
        )
        if config.eval.render:
            rl_config.render = True
        eval_mjx(rl_config)

    else:
        raise ValueError(f"Unknown training method: {config.method}")

    glog.info("Done.")


if __name__ == "__main__":
    main(sys.argv)
