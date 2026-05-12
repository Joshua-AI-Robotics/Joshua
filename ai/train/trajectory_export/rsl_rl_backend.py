"""RSL-RL backend for trajectory export."""

from __future__ import annotations

import gymnasium as gym
import numpy as np
import torch
from isaac_lab.task_builder import build_task_from_config  # noqa: F401
from isaaclab_tasks.utils import load_cfg_from_registry, parse_env_cfg
from trajectory_export.exporter import export_trajectory_data


def _resolve_task(cfg: dict) -> str:
    """Build the task from proto-defined task_config and return the gym ID."""
    if "task_config" not in cfg:
        raise ValueError(
            "Missing 'task_config' in config. All tasks must be defined "
            "via task_config in the .pbtxt preset."
        )
    _, _, gym_id = build_task_from_config(cfg)
    return gym_id


def _apply_agent_overrides(agent_cfg, cfg: dict) -> None:
    """Patch agent config from JSON ppo/network/seed."""
    ppo = cfg.get("ppo", {})
    if ppo:
        algo = agent_cfg.algorithm
        for src, dst in [
            ("learning_rate", "learning_rate"),
            ("gamma", "gamma"),
            ("gae_lambda", "lam"),
            ("clip_epsilon", "clip_param"),
            ("entropy_coef", "entropy_coef"),
            ("max_grad_norm", "max_grad_norm"),
            ("num_learning_epochs", "num_learning_epochs"),
            ("num_minibatches", "num_mini_batches"),
            ("desired_kl", "desired_kl"),
            ("vf_coeff", "value_loss_coef"),
        ]:
            if src in ppo:
                setattr(algo, dst, ppo[src])
        if "schedule" in ppo:
            algo.schedule = ppo["schedule"]
        if "use_clipped_value_loss" in ppo:
            algo.use_clipped_value_loss = ppo["use_clipped_value_loss"]
        if "num_steps_per_env" in ppo:
            agent_cfg.num_steps_per_env = ppo["num_steps_per_env"]

    net = cfg.get("network", {})
    if net and hasattr(agent_cfg, "policy"):
        policy = agent_cfg.policy
        if "actor_hidden_dims" in net:
            policy.actor_hidden_dims = list(net["actor_hidden_dims"])
        if "critic_hidden_dims" in net:
            policy.critic_hidden_dims = list(net["critic_hidden_dims"])
        if "activation" in net:
            policy.activation = net["activation"]
        if "init_noise_std" in net:
            policy.init_noise_std = net["init_noise_std"]
    if net and "empirical_normalization" in net:
        agent_cfg.empirical_normalization = net["empirical_normalization"]


def _apply_env_overrides(env_cfg, cfg: dict) -> None:
    """Patch env config from JSON sim_physics/termination/reset/target."""
    sp = cfg.get("sim_physics", {})
    if sp:
        if "decimation" in sp:
            env_cfg.decimation = int(sp["decimation"])
        if "episode_length_s" in sp:
            env_cfg.episode_length_s = sp["episode_length_s"]
        if "sim_dt" in sp:
            env_cfg.sim.dt = sp["sim_dt"]
        if "action_scale" in sp:
            for attr_name in dir(env_cfg.actions):
                attr = getattr(env_cfg.actions, attr_name, None)
                if hasattr(attr, "scale"):
                    attr.scale = sp["action_scale"]
        if "bounce_threshold_velocity" in sp:
            env_cfg.sim.physx.bounce_threshold_velocity = sp[
                "bounce_threshold_velocity"
            ]
        if "terrain_friction" in sp:
            env_cfg.sim.physics_material.static_friction = sp["terrain_friction"]
            env_cfg.sim.physics_material.dynamic_friction = sp["terrain_friction"]

    term = cfg.get("termination", {})
    if term and "min_root_height" in term:
        for attr_name in dir(env_cfg.terminations):
            t = getattr(env_cfg.terminations, attr_name, None)
            if hasattr(t, "params") and isinstance(t.params, dict):
                if "minimum_height" in t.params:
                    t.params["minimum_height"] = term["min_root_height"]

    rst = cfg.get("reset", {})
    if rst:
        for attr_name in dir(env_cfg.events):
            ev = getattr(env_cfg.events, attr_name, None)
            if hasattr(ev, "params") and isinstance(ev.params, dict):
                if "position_range" in ev.params and isinstance(
                    ev.params["position_range"], tuple
                ):
                    ev.params["position_range"] = (
                        rst.get("joint_pos_range_min", -0.2),
                        rst.get("joint_pos_range_max", 0.2),
                    )
                if "velocity_range" in ev.params and isinstance(
                    ev.params["velocity_range"], tuple
                ):
                    ev.params["velocity_range"] = (
                        rst.get("joint_vel_range_min", -0.1),
                        rst.get("joint_vel_range_max", 0.1),
                    )

    target = cfg.get("target", {})
    if target:
        target_pos = (
            target.get("x", 1000.0),
            target.get("y", 0.0),
            target.get("z", 0.0),
        )
        for group_name in ("rewards", "observations"):
            group = getattr(env_cfg, group_name, None)
            if group is None:
                continue
            for attr_name in dir(group):
                t = getattr(group, attr_name, None)
                if hasattr(t, "params") and isinstance(t.params, dict):
                    if "target_pos" in t.params:
                        t.params["target_pos"] = target_pos


def trajectory_export_rsl_rl(cfg: dict) -> None:
    """Export trajectory from an RSL-RL checkpoint."""
    from isaaclab_rl.rsl_rl import RslRlVecEnvWrapper
    from rsl_rl.runners import OnPolicyRunner

    isaac_task = _resolve_task(cfg)
    checkpoint_path = cfg["checkpoint_path"]
    device = "cuda" if torch.cuda.is_available() else "cpu"

    env_cfg = parse_env_cfg(isaac_task, num_envs=1)
    _apply_env_overrides(env_cfg, cfg)
    env = gym.make(isaac_task, cfg=env_cfg)

    raw_env = env.unwrapped
    joint_names = list(raw_env.scene["robot"].data.joint_names)
    step_dt = raw_env.cfg.sim.dt * raw_env.cfg.decimation

    env = RslRlVecEnvWrapper(env)

    agent_cfg = load_cfg_from_registry(isaac_task, "rsl_rl_cfg_entry_point")
    _apply_agent_overrides(agent_cfg, cfg)
    agent_cfg.device = device

    runner = OnPolicyRunner(
        env,
        agent_cfg.to_dict(),
        log_dir=None,
        device=device,
    )
    runner.load(checkpoint_path)

    warmup_steps = cfg.get("warmup_steps", 200)
    num_record_steps = cfg.get("num_record_steps", 1000)
    num_joints = len(joint_names)

    print(f"[Joshua/Isaac] Joints ({num_joints}): {joint_names}")
    print(
        f"[Joshua/Isaac] dt={step_dt:.6f}s  "
        f"warmup={warmup_steps}  record={num_record_steps}"
    )

    obs = env.get_observations()
    for _ in range(warmup_steps):
        with torch.inference_mode():
            actions = runner.alg.act(obs)
        obs, _, _, dones, _ = env.step(actions)

    all_positions = np.zeros((num_record_steps, num_joints))
    all_actions = np.zeros((num_record_steps, num_joints))

    for t in range(num_record_steps):
        with torch.inference_mode():
            actions = runner.alg.act(obs)
        obs, _, _, dones, _ = env.step(actions)
        all_positions[t] = raw_env.scene["robot"].data.joint_pos[0].cpu().numpy()
        all_actions[t] = actions[0].cpu().numpy()

    env.close()
    export_trajectory_data(all_positions, all_actions, step_dt, cfg, joint_names)
