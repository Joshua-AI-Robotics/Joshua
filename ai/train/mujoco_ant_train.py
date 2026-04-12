"""Train a MuJoCo-native Ant PPO policy with skrl."""

from __future__ import annotations

import argparse
import os

import gymnasium as gym
import torch
from skrl.agents.torch.ppo import PPO, PPO_DEFAULT_CONFIG
from skrl.envs.wrappers.torch import wrap_env
from skrl.memories.torch import RandomMemory
from skrl.trainers.torch import SequentialTrainer
from skrl.utils import set_seed

from ai.train.mujoco_ant_common import Policy, Value


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Train a MuJoCo Ant PPO policy.")
    parser.add_argument("--timesteps", type=int, default=100_000)
    parser.add_argument("--rollouts", type=int, default=1024)
    parser.add_argument("--learning_epochs", type=int, default=10)
    parser.add_argument("--mini_batches", type=int, default=32)
    parser.add_argument("--learning_rate", type=float, default=3e-4)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--hidden_dim", type=int, default=256)
    parser.add_argument("--log_dir", default="logs/mujoco_ant")
    parser.add_argument("--experiment_name", default="ppo_ant_v5")
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    set_seed(args.seed)

    env = gym.make("Ant-v5", render_mode=None)
    env = wrap_env(env)

    obs_size = env.observation_space.shape[-1]
    action_size = env.action_space.shape[-1]
    hidden_dims = [args.hidden_dim, args.hidden_dim]

    memory = RandomMemory(memory_size=args.rollouts, num_envs=1, device=device)
    models = {
        "policy": Policy(
            env.observation_space,
            env.action_space,
            device,
            obs_size,
            action_size,
            hidden_dims,
        ),
        "value": Value(
            env.observation_space,
            env.action_space,
            device,
            obs_size,
            hidden_dims,
        ),
    }

    cfg = PPO_DEFAULT_CONFIG.copy()
    cfg.update(
        {
            "rollouts": args.rollouts,
            "learning_epochs": args.learning_epochs,
            "mini_batches": args.mini_batches,
            "discount_factor": 0.99,
            "lambda": 0.95,
            "learning_rate": args.learning_rate,
            "grad_norm_clip": 1.0,
            "ratio_clip": 0.2,
            "value_clip": 0.2,
            "entropy_loss_scale": 0.0,
            "value_loss_scale": 1.0,
            "experiment": {
                "directory": os.path.abspath(args.log_dir),
                "experiment_name": args.experiment_name,
                "write_interval": 1000,
                "checkpoint_interval": 10_000,
            },
        }
    )

    agent = PPO(
        models=models,
        memory=memory,
        cfg=cfg,
        observation_space=env.observation_space,
        action_space=env.action_space,
        device=device,
    )

    trainer = SequentialTrainer(
        env=env,
        agents=agent,
        cfg={"timesteps": args.timesteps, "headless": True},
    )

    print(f"TRAIN_TIMESTEPS={args.timesteps}", flush=True)
    print(f"TRAIN_DEVICE={device}", flush=True)
    print(f"TRAIN_LOG_DIR={os.path.abspath(args.log_dir)}", flush=True)
    trainer.train()

    final_path = os.path.join(
        os.path.abspath(args.log_dir), args.experiment_name, "final_policy.pt"
    )
    os.makedirs(os.path.dirname(final_path), exist_ok=True)
    agent.save(final_path)
    print(f"FINAL_POLICY={final_path}", flush=True)
    env.close()


if __name__ == "__main__":
    main()
