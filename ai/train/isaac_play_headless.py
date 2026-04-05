#!/usr/bin/env python3
"""Minimal headless Isaac Lab checkpoint runner.

This script is intended for machines where the full Isaac Lab play.py
render/video path is unreliable, but the simulator itself can still run
headless. It loads a checkpoint, steps the policy for a fixed number of
timesteps, and prints simple rollout statistics.

Example:
    C:\path\to\env_isaaclab_51\Scripts\python.exe ai/train/isaac_play_headless.py \
        --task Isaac-Ant-v0 \
        --checkpoint C:\path\to\agent_16.pt \
        --num_envs 1 \
        --steps 200
"""

from __future__ import annotations

import argparse
import os

# Set the EULA flag before any Isaac Sim imports.
os.environ.setdefault("OMNI_KIT_ACCEPT_EULA", "YES")

from isaaclab.app import AppLauncher  # noqa: E402


def _build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run an Isaac Lab checkpoint headlessly without video rendering."
    )
    parser.add_argument("--task", required=True, help="Isaac Lab task name, e.g. Isaac-Ant-v0.")
    parser.add_argument("--checkpoint", required=True, help="Path to the checkpoint file.")
    parser.add_argument("--num_envs", type=int, default=1, help="Number of environments.")
    parser.add_argument("--steps", type=int, default=200, help="Number of rollout steps.")
    parser.add_argument("--seed", type=int, default=42, help="Environment seed.")
    parser.add_argument(
        "--sim_device",
        default="cuda:0",
        help="Isaac device string. Use cpu if CUDA is unavailable.",
    )
    AppLauncher.add_app_launcher_args(parser)
    return parser


parser = _build_arg_parser()
args_cli = parser.parse_args(["--headless", *os.sys.argv[1:]])
app_launcher = AppLauncher(args_cli)
simulation_app = app_launcher.app

import gymnasium as gym  # noqa: E402
import torch  # noqa: E402
from skrl.utils.runner.torch import Runner  # noqa: E402

import isaaclab_tasks  # noqa: E402,F401
from isaaclab_rl.skrl import SkrlVecEnvWrapper  # noqa: E402
from isaaclab_tasks.utils import load_cfg_from_registry, parse_env_cfg  # noqa: E402


def main() -> None:
    env_cfg = parse_env_cfg(
        args_cli.task, num_envs=args_cli.num_envs, device=args_cli.sim_device
    )
    env_cfg.seed = args_cli.seed

    agent_cfg = load_cfg_from_registry(args_cli.task, "skrl_cfg_entry_point")
    agent_cfg["seed"] = args_cli.seed
    agent_cfg["trainer"]["close_environment_at_exit"] = False
    agent_cfg["agent"]["experiment"]["write_interval"] = 0
    agent_cfg["agent"]["experiment"]["checkpoint_interval"] = 0

    env = gym.make(args_cli.task, cfg=env_cfg)
    env = SkrlVecEnvWrapper(env, ml_framework="torch")

    runner = Runner(env, agent_cfg)
    runner.agent.load(os.path.abspath(args_cli.checkpoint))
    runner.agent.set_running_mode("eval")

    obs, _ = env.reset()
    reward_sum = torch.zeros(args_cli.num_envs, device=env.device)
    done_count = 0

    for step in range(args_cli.steps):
        with torch.inference_mode():
            outputs = runner.agent.act(obs, timestep=step, timesteps=args_cli.steps)
            actions = outputs[-1].get("mean_actions", outputs[0])
            obs, rewards, terminated, truncated, _ = env.step(actions)

        reward_sum += rewards.reshape(-1)
        done_mask = torch.logical_or(terminated, truncated)
        done_count += int(done_mask.sum().item())

    reward_mean = float(reward_sum.mean().item())
    reward_min = float(reward_sum.min().item())
    reward_max = float(reward_sum.max().item())

    print(f"TASK={args_cli.task}", flush=True)
    print(f"CHECKPOINT={os.path.abspath(args_cli.checkpoint)}", flush=True)
    print(f"STEPS={args_cli.steps}", flush=True)
    print(f"NUM_ENVS={args_cli.num_envs}", flush=True)
    print(f"DONE_COUNT={done_count}", flush=True)
    print(f"EPISODE_REWARD_MEAN={reward_mean:.6f}", flush=True)
    print(f"EPISODE_REWARD_MIN={reward_min:.6f}", flush=True)
    print(f"EPISODE_REWARD_MAX={reward_max:.6f}", flush=True)

    env.close()
    simulation_app.close()


if __name__ == "__main__":
    main()
