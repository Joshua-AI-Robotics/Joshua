"""skrl backend for trajectory export."""

from __future__ import annotations

import gymnasium as gym
import numpy as np
import torch
from isaac_lab.task_builder import build_task_from_config  # noqa: F401
from isaaclab_tasks.utils import load_cfg_from_registry, parse_env_cfg
from trajectory_export.exporter import export_trajectory_data
from trajectory_export.rsl_rl_backend import (
    _apply_agent_overrides,
    _apply_env_overrides,
)


def _resolve_task(cfg: dict) -> str:
    """Build the task from proto-defined task_config and return the gym ID."""
    if "task_config" not in cfg:
        raise ValueError(
            "Missing 'task_config' in config. All tasks must be defined "
            "via task_config in the .pbtxt preset."
        )
    _, _, gym_id = build_task_from_config(cfg)
    return gym_id


def trajectory_export_skrl(cfg: dict) -> None:
    """Export trajectory from a skrl checkpoint."""
    import skrl  # noqa: F401
    from skrl.agents.torch.ppo import PPO, PPO_DEFAULT_CONFIG
    from skrl.envs.wrappers.torch import wrap_env
    from skrl.memories.torch import RandomMemory
    from skrl.models.torch import DeterministicMixin, GaussianMixin, Model

    isaac_task = _resolve_task(cfg)
    checkpoint_path = cfg["checkpoint_path"]
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    env_cfg = parse_env_cfg(isaac_task, num_envs=1)
    _apply_env_overrides(env_cfg, cfg)
    env = gym.make(isaac_task, cfg=env_cfg)

    raw_env = env.unwrapped
    joint_names = list(raw_env.scene["robot"].data.joint_names)
    step_dt = raw_env.cfg.sim.dt * raw_env.cfg.decimation

    env = wrap_env(env, wrapper="isaaclab")

    try:
        agent_cfg = load_cfg_from_registry(isaac_task, "rsl_rl_cfg_entry_point")
    except KeyError:
        agent_cfg = None

    if agent_cfg is not None:
        _apply_agent_overrides(agent_cfg, cfg)

    obs_size = env.observation_space.shape[-1]
    action_size = env.action_space.shape[-1]
    hidden = agent_cfg.policy.actor_hidden_dims if agent_cfg else [256, 256]

    class Policy(GaussianMixin, Model):
        def __init__(self, observation_space, action_space, dev, **kwargs):
            Model.__init__(self, observation_space, action_space, dev)
            GaussianMixin.__init__(self, min_log_std=-20.0, max_log_std=2.0)
            layers = []
            in_dim = obs_size
            for h in hidden:
                layers += [torch.nn.Linear(in_dim, h), torch.nn.ELU()]
                in_dim = h
            layers.append(torch.nn.Linear(in_dim, action_size))
            self.net = torch.nn.Sequential(*layers)
            self.log_std_parameter = torch.nn.Parameter(torch.zeros(action_size))

        def compute(self, inputs, role=""):
            return (self.net(inputs["states"]), self.log_std_parameter, {})

    class Value(DeterministicMixin, Model):
        def __init__(self, observation_space, action_space, dev, **kwargs):
            Model.__init__(self, observation_space, action_space, dev)
            DeterministicMixin.__init__(self)
            c_hidden = agent_cfg.policy.critic_hidden_dims if agent_cfg else [256, 256]
            layers = []
            in_dim = obs_size
            for h in c_hidden:
                layers += [torch.nn.Linear(in_dim, h), torch.nn.ELU()]
                in_dim = h
            layers.append(torch.nn.Linear(in_dim, 1))
            self.net = torch.nn.Sequential(*layers)

        def compute(self, inputs, role=""):
            return self.net(inputs["states"]), {}

    models = {
        "policy": Policy(env.observation_space, env.action_space, device),
        "value": Value(env.observation_space, env.action_space, device),
    }

    rollout_steps = agent_cfg.num_steps_per_env if agent_cfg else 24
    memory = RandomMemory(memory_size=rollout_steps, num_envs=1, device=device)

    ppo_cfg = PPO_DEFAULT_CONFIG.copy()
    agent = PPO(
        models=models,
        memory=memory,
        cfg=ppo_cfg,
        observation_space=env.observation_space,
        action_space=env.action_space,
        device=device,
    )
    agent.load(checkpoint_path)
    agent.set_running_mode("eval")

    warmup_steps = cfg.get("warmup_steps", 200)
    num_record_steps = cfg.get("num_record_steps", 1000)
    num_joints = len(joint_names)

    print(f"[Joshua/Isaac] Joints ({num_joints}): {joint_names}")
    print(
        f"[Joshua/Isaac] dt={step_dt:.6f}s  "
        f"warmup={warmup_steps}  record={num_record_steps}"
    )

    obs, _ = env.reset()
    for _ in range(warmup_steps):
        with torch.inference_mode():
            outputs = agent.act(obs, timestep=0, timesteps=0)
            actions = outputs[-1].get("mean_actions", outputs[0])
            obs, _, _, _, _ = env.step(actions)

    all_positions = np.zeros((num_record_steps, num_joints))
    all_actions = np.zeros((num_record_steps, num_joints))

    for t in range(num_record_steps):
        with torch.inference_mode():
            outputs = agent.act(obs, timestep=0, timesteps=0)
            actions = outputs[-1].get("mean_actions", outputs[0])
            obs, _, _, _, _ = env.step(actions)
        all_positions[t] = raw_env.scene["robot"].data.joint_pos[0].cpu().numpy()
        all_actions[t] = actions[0].cpu().numpy()

    env.close()
    export_trajectory_data(all_positions, all_actions, step_dt, cfg, joint_names)
