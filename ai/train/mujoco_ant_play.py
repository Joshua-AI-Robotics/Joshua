"""Render a MuJoCo-native Ant PPO policy with skrl."""

from __future__ import annotations

import argparse

import gymnasium as gym
import torch
from skrl.agents.torch.ppo import PPO, PPO_DEFAULT_CONFIG
from skrl.memories.torch import RandomMemory

from ai.train.mujoco_ant_common import Policy, Value


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Play a MuJoCo Ant PPO policy.")
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--episodes", type=int, default=3)
    parser.add_argument("--hidden_dim", type=int, default=256)
    return parser.parse_args()


def main() -> None:
    args = parse_args()

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    env = gym.make("Ant-v5", render_mode="human")

    obs_size = env.observation_space.shape[-1]
    action_size = env.action_space.shape[-1]
    hidden_dims = [args.hidden_dim, args.hidden_dim]

    memory = RandomMemory(memory_size=1, num_envs=1, device=device)
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
    agent = PPO(
        models=models,
        memory=memory,
        cfg=PPO_DEFAULT_CONFIG.copy(),
        observation_space=env.observation_space,
        action_space=env.action_space,
        device=device,
    )
    agent.load(args.checkpoint)
    agent.set_running_mode("eval")

    for episode in range(args.episodes):
        obs, _ = env.reset()
        done = False
        truncated = False
        total_reward = 0.0
        while not done and not truncated:
            state_tensor = torch.as_tensor(
                obs, device=device, dtype=torch.float32
            ).unsqueeze(0)
            with torch.inference_mode():
                outputs = agent.act(state_tensor, timestep=0, timesteps=0)
            actions = outputs[-1].get("mean_actions", outputs[0]).squeeze(0)
            obs, reward, done, truncated, _ = env.step(actions.cpu().numpy())
            total_reward += reward
            env.render()
        print(f"EPISODE={episode} REWARD={total_reward:.3f}", flush=True)

    env.close()


if __name__ == "__main__":
    main()
