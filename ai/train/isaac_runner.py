#!/usr/bin/env python3
"""Isaac Lab training bridge for Joshua.

This script runs inside Isaac Lab's Python environment (NOT Bazel).
Communication with Joshua is via a JSON config file written by
``isaac_launcher.py``.

Environment configs, agent configs, and MDP terms are defined in the
``isaac_lab`` package (shipped alongside this script) -- nothing is
hardcoded here.

Usage (called automatically by isaac_launcher.py):
    isaaclab.sh -p ai/train/isaac_runner.py --config /tmp/joshua_cfg.json --headless
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from datetime import datetime

# ── Make isaac_lab importable (lives next to this script) ──────────

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

# ── CLI + AppLauncher init (must happen before other Isaac imports) ────

parser = argparse.ArgumentParser(
    description="Joshua-to-Isaac-Lab training bridge."
)
parser.add_argument(
    "--config", required=True,
    help="Path to Joshua JSON config written by isaac_launcher.py",
)

from isaaclab.app import AppLauncher  # noqa: E402

AppLauncher.add_app_launcher_args(parser)
args_cli = parser.parse_args()

app_launcher = AppLauncher(args_cli)
simulation_app = app_launcher.app

# ── Imports that require Isaac Sim to be running ──────────────────────

import gymnasium as gym  # noqa: E402
import torch  # noqa: E402

import isaaclab_tasks  # noqa: E402, F401
import isaac_lab  # noqa: E402, F401
from isaaclab_tasks.utils import load_cfg_from_registry, parse_env_cfg  # noqa: E402

torch.backends.cuda.matmul.allow_tf32 = True
torch.backends.cudnn.allow_tf32 = True
torch.backends.cudnn.deterministic = False
torch.backends.cudnn.benchmark = False


def _log(message: str) -> None:
    """Emit a timestamped log line and flush immediately."""
    ts = datetime.now().strftime("%H:%M:%S")
    print(f"[Joshua/Isaac {ts}] {message}", flush=True)


# ── Config override system ───────────────────────────────────────────

def _apply_agent_overrides(agent_cfg, cfg: dict) -> None:
    """Patch agent config from JSON ppo/network/seed/save_interval."""
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

    if cfg.get("save_interval"):
        agent_cfg.save_interval = cfg["save_interval"]
    if cfg.get("seed"):
        agent_cfg.seed = cfg["seed"]

    max_iters = cfg.get("max_iterations", 0)
    if max_iters:
        agent_cfg.max_iterations = max_iters


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
            env_cfg.sim.physx.bounce_threshold_velocity = sp["bounce_threshold_velocity"]
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
        target_pos = (target.get("x", 1000.0),
                      target.get("y", 0.0),
                      target.get("z", 0.0))
        for group_name in ("rewards", "observations"):
            group = getattr(env_cfg, group_name, None)
            if group is None:
                continue
            for attr_name in dir(group):
                t = getattr(group, attr_name, None)
                if hasattr(t, "params") and isinstance(t.params, dict):
                    if "target_pos" in t.params:
                        t.params["target_pos"] = target_pos

    # Override individual reward weights
    rw = cfg.get("reward_weights", {})
    if rw and hasattr(env_cfg, "rewards"):
        for name, weight in rw.items():
            if hasattr(env_cfg.rewards, name):
                getattr(env_cfg.rewards, name).weight = weight


def _resolve_task(cfg: dict) -> str:
    """Build the task from proto-defined task_config and return the gym ID."""
    if "task_config" not in cfg:
        raise ValueError(
            "Missing 'task_config' in config. All tasks must be defined "
            "via task_config in the .pbtxt preset."
        )
    _log("Resolving proto-defined Isaac task via task_builder")
    from isaac_lab.task_builder import build_task_from_config
    _, _, gym_id = build_task_from_config(cfg)
    _log(f"Task resolved to gym id: {gym_id}")
    return gym_id


def _write_meta(cfg: dict, env, log_dir: str) -> None:
    """Write Joshua-compatible metadata JSON."""
    checkpoint_dir = cfg.get("checkpoint_dir", "/tmp/joshua_checkpoints")
    save_name = cfg.get("save_path", f"{cfg['task']}_isaac_ppo")

    obs_size = action_size = 0
    try:
        obs_space = getattr(env, "observation_space", None)
        act_space = getattr(env, "action_space", None)
        if obs_space is not None and obs_space.shape is not None:
            obs_size = obs_space.shape[-1]
        if act_space is not None and act_space.shape is not None:
            action_size = act_space.shape[-1]
    except Exception:
        pass

    meta = {
        "task": cfg.get("task", cfg.get("task_config", {}).get("task_name", "")),
        "isaac_task": _resolve_task(cfg),
        "backend": "isaac_sim",
        "algorithm": cfg.get("algorithm", "rsl_rl"),
        "obs_size": obs_size,
        "action_size": action_size,
        "num_envs": cfg.get("num_envs", 0),
        "checkpoint_dir": log_dir,
    }
    meta_path = os.path.join(checkpoint_dir, save_name + "_meta.json")
    os.makedirs(os.path.dirname(meta_path), exist_ok=True)
    with open(meta_path, "w") as f:
        json.dump(meta, f, indent=2)
    print(f"[Joshua/Isaac] Metadata written to {meta_path}")
    print(f"[Joshua/Isaac] Checkpoints in {log_dir}")


# ── RSL-RL backend ───────────────────────────────────────────────────

def _train_rsl_rl(cfg: dict) -> None:
    """Train using RSL-RL's OnPolicyRunner (PPO)."""
    _log("Entering RSL-RL training path")
    from rsl_rl.runners import OnPolicyRunner
    from isaaclab_rl.rsl_rl import RslRlVecEnvWrapper

    isaac_task = _resolve_task(cfg)
    num_envs = cfg.get("num_envs", 4096)
    save_name = cfg.get("save_path", f"{cfg.get('task', 'task')}_isaac_ppo")
    checkpoint_dir = cfg.get("checkpoint_dir", "/tmp/joshua_checkpoints")
    device = "cuda" if torch.cuda.is_available() else "cpu"
    _log(f"RSL-RL config: task={isaac_task}, num_envs={num_envs}, device={device}")

    _log("Parsing Isaac environment config from registry")
    env_cfg = parse_env_cfg(isaac_task, num_envs=num_envs)
    _apply_env_overrides(env_cfg, cfg)

    _log("Creating Isaac gym environment")
    env = gym.make(isaac_task, cfg=env_cfg)
    _log("Wrapping environment for RSL-RL")
    env = RslRlVecEnvWrapper(env)

    _log("Loading RSL-RL agent config from registry")
    agent_cfg = load_cfg_from_registry(isaac_task, "rsl_rl_cfg_entry_point")
    _apply_agent_overrides(agent_cfg, cfg)

    agent_cfg.experiment_name = save_name
    agent_cfg.device = device

    log_dir = os.path.join(checkpoint_dir, "isaac_logs", save_name)
    os.makedirs(log_dir, exist_ok=True)
    _log(f"RSL-RL log dir prepared at {log_dir}")

    agent_dict = agent_cfg.to_dict()
    _log("Constructing OnPolicyRunner")
    runner = OnPolicyRunner(
        env, agent_dict, log_dir=log_dir, device=device,
    )

    total_timesteps = (agent_cfg.max_iterations
                       * num_envs
                       * agent_cfg.num_steps_per_env)
    _log(
        f"RSL-RL training {isaac_task} for {agent_cfg.max_iterations} iterations "
        f"({total_timesteps:,} timesteps, {num_envs} envs)"
    )

    _write_meta(cfg, env, log_dir)

    _log("Calling runner.learn()")
    runner.learn(
        num_learning_iterations=agent_cfg.max_iterations,
        init_at_random_ep_len=True,
    )
    _log("runner.learn() returned")

    env.close()
    _log("RSL-RL environment closed")


# ── skrl backend ─────────────────────────────────────────────────────

def _train_skrl(cfg: dict) -> None:
    """Train using skrl's PPO agent."""
    _log("Entering skrl training path")
    import skrl  # noqa: F401
    from skrl.agents.torch.ppo import PPO, PPO_DEFAULT_CONFIG
    from skrl.envs.wrappers.torch import wrap_env
    from skrl.memories.torch import RandomMemory
    from skrl.models.torch import DeterministicMixin, GaussianMixin, Model
    from skrl.trainers.torch import SequentialTrainer
    from skrl.utils import set_seed

    isaac_task = _resolve_task(cfg)
    num_envs = cfg.get("num_envs", 4096)
    save_name = cfg.get("save_path", f"{cfg.get('task', 'task')}_isaac_ppo")
    checkpoint_dir = cfg.get("checkpoint_dir", "/tmp/joshua_checkpoints")
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    _log(
        f"skrl config: task={isaac_task}, num_envs={num_envs}, "
        f"device={device}, save_name={save_name}"
    )

    _log("Parsing Isaac environment config from registry")
    env_cfg = parse_env_cfg(isaac_task, num_envs=num_envs)
    _apply_env_overrides(env_cfg, cfg)

    _log("Creating Isaac gym environment")
    env = gym.make(isaac_task, cfg=env_cfg)
    _log("Wrapping environment for skrl")
    env = wrap_env(env, wrapper="isaaclab")
    _log(
        f"Environment ready: obs_shape={getattr(env.observation_space, 'shape', None)}, "
        f"action_shape={getattr(env.action_space, 'shape', None)}"
    )

    try:
        _log("Attempting to load optional registry agent config")
        agent_cfg = load_cfg_from_registry(isaac_task, "rsl_rl_cfg_entry_point")
    except (KeyError, ValueError):
        agent_cfg = None
        _log("No registry agent config found for skrl path; using Joshua config only")

    if agent_cfg is not None:
        _log("Applying agent overrides from Joshua config")
        _apply_agent_overrides(agent_cfg, cfg)

    obs_size = env.observation_space.shape[-1]
    action_size = env.action_space.shape[-1]
    _log(f"Derived network sizes: obs_size={obs_size}, action_size={action_size}")

    if agent_cfg is not None:
        hidden = agent_cfg.policy.actor_hidden_dims
    else:
        net = cfg.get("network", {})
        hidden = list(net.get("actor_hidden_dims", [256, 256]))

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
            self.log_std_parameter = torch.nn.Parameter(
                torch.zeros(action_size))

        def compute(self, inputs, role=""):
            return self.net(inputs["states"]), self.log_std_parameter, {}

    class Value(DeterministicMixin, Model):
        def __init__(self, observation_space, action_space, dev, **kwargs):
            Model.__init__(self, observation_space, action_space, dev)
            DeterministicMixin.__init__(self)
            if agent_cfg:
                c_hidden = agent_cfg.policy.critic_hidden_dims
            else:
                net = cfg.get("network", {})
                c_hidden = list(net.get("critic_hidden_dims", [256, 256]))
            layers = []
            in_dim = obs_size
            for h in c_hidden:
                layers += [torch.nn.Linear(in_dim, h), torch.nn.ELU()]
                in_dim = h
            layers.append(torch.nn.Linear(in_dim, 1))
            self.net = torch.nn.Sequential(*layers)

        def compute(self, inputs, role=""):
            return self.net(inputs["states"]), {}

    seed_val = cfg.get("seed", 0) or (agent_cfg.seed if agent_cfg else 42)
    _log(f"Setting random seed: {seed_val}")
    set_seed(seed_val)

    ppo_cfg_json = cfg.get("ppo", {})
    rollout_steps = (ppo_cfg_json.get("num_steps_per_env")
                     or (agent_cfg.num_steps_per_env if agent_cfg else 24))
    max_iterations = cfg.get("max_iterations", 0)
    if not max_iterations:
        max_iterations = (agent_cfg.max_iterations if agent_cfg else 500)
    total_timesteps = max_iterations * rollout_steps
    _log(
        f"Training schedule: rollout_steps={rollout_steps}, "
        f"max_iterations={max_iterations}, total_timesteps={total_timesteps}"
    )

    _log("Allocating skrl rollout memory")
    memory = RandomMemory(
        memory_size=rollout_steps, num_envs=num_envs, device=device)

    _log("Constructing policy/value models")
    models = {
        "policy": Policy(env.observation_space, env.action_space, device),
        "value": Value(env.observation_space, env.action_space, device),
    }

    ppo_cfg = PPO_DEFAULT_CONFIG.copy()
    if agent_cfg is not None:
        algo = agent_cfg.algorithm
        ppo_cfg.update({
            "rollouts": rollout_steps,
            "learning_epochs": algo.num_learning_epochs,
            "mini_batches": algo.num_mini_batches,
            "discount_factor": algo.gamma,
            "lambda": algo.lam,
            "learning_rate": algo.learning_rate,
            "grad_norm_clip": algo.max_grad_norm,
            "ratio_clip": algo.clip_param,
            "value_clip": algo.clip_param,
            "entropy_loss_scale": algo.entropy_coef,
            "value_loss_scale": algo.value_loss_coef,
        })
    else:
        ppo_cfg.update({
            "rollouts": rollout_steps,
            "learning_epochs": int(ppo_cfg_json.get("num_learning_epochs", 5)),
            "mini_batches": int(ppo_cfg_json.get("num_minibatches", 4)),
            "discount_factor": ppo_cfg_json.get("gamma", 0.99),
            "lambda": ppo_cfg_json.get("gae_lambda", 0.95),
            "learning_rate": ppo_cfg_json.get("learning_rate", 3e-4),
            "grad_norm_clip": ppo_cfg_json.get("max_grad_norm", 1.0),
            "ratio_clip": ppo_cfg_json.get("clip_epsilon", 0.2),
            "value_clip": ppo_cfg_json.get("clip_epsilon", 0.2),
            "entropy_loss_scale": ppo_cfg_json.get("entropy_coef", 0.01),
            "value_loss_scale": ppo_cfg_json.get("vf_coeff", 1.0),
        })

    log_dir = os.path.join(checkpoint_dir, "isaac_logs", save_name)
    os.makedirs(log_dir, exist_ok=True)
    _log(f"skrl log dir prepared at {log_dir}")

    _log("Constructing PPO agent")
    agent = PPO(
        models=models,
        memory=memory,
        cfg=ppo_cfg,
        observation_space=env.observation_space,
        action_space=env.action_space,
        device=device,
    )

    trainer_cfg = {
        "timesteps": total_timesteps,
        "headless": True,
    }

    _log(f"Constructing SequentialTrainer with cfg={trainer_cfg}")
    trainer = SequentialTrainer(env=env, agents=agent, cfg=trainer_cfg)

    _log(
        f"skrl training {isaac_task} for {max_iterations} iterations "
        f"({total_timesteps:,} timesteps, {num_envs} envs)"
    )
    _write_meta(cfg, env, log_dir)

    _log("Calling trainer.train()")
    trainer.train()
    _log("trainer.train() returned")

    _log("Saving final policy checkpoint")
    agent.save(os.path.join(log_dir, "final_policy.pt"))
    _log("Final policy checkpoint saved")
    env.close()
    _log("skrl environment closed")


# ── Evaluation ───────────────────────────────────────────────────────

def _run_eval(cfg: dict) -> None:
    """Run evaluation using a trained checkpoint."""
    isaac_task = _resolve_task(cfg)
    algorithm = cfg.get("algorithm", "skrl")
    num_episodes = cfg.get("num_episodes", 5)
    num_envs = cfg.get("num_envs", 32)
    checkpoint_path = cfg.get("checkpoint_path", "")
    if not checkpoint_path:
        raise ValueError("checkpoint_path is required for evaluation")
    if not os.path.isfile(checkpoint_path):
        raise FileNotFoundError(f"Checkpoint not found: {checkpoint_path}")

    print(f"[Joshua/Isaac] Evaluating {isaac_task} ({algorithm}) "
          f"for {num_episodes} episodes, {num_envs} envs")
    print(f"[Joshua/Isaac] Checkpoint: {checkpoint_path}")

    if algorithm == "skrl":
        _eval_skrl(cfg, isaac_task, checkpoint_path, num_envs, num_episodes)
    elif algorithm == "rsl_rl":
        _eval_rsl_rl(cfg, isaac_task, checkpoint_path, num_envs, num_episodes)
    else:
        raise ValueError(f"Unknown algorithm '{algorithm}' for eval")


def _eval_skrl(cfg, isaac_task, checkpoint_path, num_envs, num_episodes):
    """Evaluate a skrl PPO checkpoint."""
    import skrl  # noqa: F401
    from skrl.agents.torch.ppo import PPO, PPO_DEFAULT_CONFIG
    from skrl.envs.wrappers.torch import wrap_env
    from skrl.memories.torch import RandomMemory
    from skrl.models.torch import DeterministicMixin, GaussianMixin, Model

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    env_cfg = parse_env_cfg(isaac_task, num_envs=num_envs)
    _apply_env_overrides(env_cfg, cfg)
    env = gym.make(isaac_task, cfg=env_cfg)
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
            self.log_std_parameter = torch.nn.Parameter(
                torch.zeros(action_size))

        def compute(self, inputs, role=""):
            return self.net(inputs["states"]), self.log_std_parameter, {}

    class Value(DeterministicMixin, Model):
        def __init__(self, observation_space, action_space, dev, **kwargs):
            Model.__init__(self, observation_space, action_space, dev)
            DeterministicMixin.__init__(self)
            c_hidden = (agent_cfg.policy.critic_hidden_dims
                        if agent_cfg else [256, 256])
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
    memory = RandomMemory(
        memory_size=rollout_steps, num_envs=num_envs, device=device)

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

    print(f"[Joshua/Isaac] Loaded checkpoint, running {num_episodes} episodes")

    obs, _ = env.reset()
    episode_count = 0
    episode_rewards = torch.zeros(num_envs, device=device)
    all_rewards = []

    while episode_count < num_episodes:
        with torch.inference_mode():
            outputs = agent.act(obs, timestep=0, timesteps=0)
            actions = outputs[-1].get("mean_actions", outputs[0])
            obs, rewards, terminated, truncated, infos = env.step(actions)

        episode_rewards += rewards.squeeze()
        dones = (terminated | truncated).squeeze()
        if dones.any():
            finished = dones.nonzero(as_tuple=False).squeeze(-1)
            for idx in finished:
                all_rewards.append(episode_rewards[idx].item())
                episode_count += 1
                if episode_count >= num_episodes:
                    break
            episode_rewards[dones] = 0.0

    import statistics
    mean_r = statistics.mean(all_rewards[:num_episodes])
    std_r = statistics.stdev(all_rewards[:num_episodes]) if num_episodes > 1 else 0.0
    print(f"[Joshua/Isaac] Eval complete: {num_episodes} episodes")
    print(f"[Joshua/Isaac]   Mean reward: {mean_r:.2f} +/- {std_r:.2f}")
    env.close()


def _eval_rsl_rl(cfg, isaac_task, checkpoint_path, num_envs, num_episodes):
    """Evaluate an RSL-RL checkpoint."""
    from rsl_rl.runners import OnPolicyRunner
    from isaaclab_rl.rsl_rl import RslRlVecEnvWrapper

    device = "cuda" if torch.cuda.is_available() else "cpu"

    env_cfg = parse_env_cfg(isaac_task, num_envs=num_envs)
    _apply_env_overrides(env_cfg, cfg)
    env = gym.make(isaac_task, cfg=env_cfg)
    env = RslRlVecEnvWrapper(env)

    agent_cfg = load_cfg_from_registry(isaac_task, "rsl_rl_cfg_entry_point")
    _apply_agent_overrides(agent_cfg, cfg)
    agent_cfg.device = device

    runner = OnPolicyRunner(
        env, agent_cfg.to_dict(), log_dir=None, device=device,
    )
    runner.load(checkpoint_path)

    print(f"[Joshua/Isaac] Loaded checkpoint, running {num_episodes} episodes")

    obs = env.get_observations()
    episode_count = 0
    episode_rewards = torch.zeros(num_envs, device=torch.device(device))
    all_rewards = []

    while episode_count < num_episodes:
        with torch.inference_mode():
            actions = runner.alg.act(obs)
        obs, _, rewards, dones, infos = env.step(actions)

        episode_rewards += rewards.squeeze()
        if dones.any():
            finished = dones.nonzero(as_tuple=False).squeeze(-1)
            for idx in finished:
                all_rewards.append(episode_rewards[idx].item())
                episode_count += 1
                if episode_count >= num_episodes:
                    break
            episode_rewards[dones] = 0.0

    import statistics
    mean_r = statistics.mean(all_rewards[:num_episodes])
    std_r = statistics.stdev(all_rewards[:num_episodes]) if num_episodes > 1 else 0.0
    print(f"[Joshua/Isaac] Eval complete: {num_episodes} episodes")
    print(f"[Joshua/Isaac]   Mean reward: {mean_r:.2f} +/- {std_r:.2f}")
    env.close()


# ── Entry point ──────────────────────────────────────────────────────

_TRAINERS = {
    "rsl_rl": _train_rsl_rl,
    "skrl": _train_skrl,
}


def main():
    _log(f"Opening Joshua config: {args_cli.config}")
    with open(args_cli.config) as f:
        cfg = json.load(f)

    mode = cfg.get("mode", "train")
    algorithm = cfg.get("algorithm", "rsl_rl")
    _log(
        f"Loaded config: mode={mode}, algorithm={algorithm}, "
        f"task_name={cfg.get('task_config', {}).get('task_name', '')}, "
        f"num_envs={cfg.get('num_envs', '')}"
    )

    if mode == "eval":
        _log("Dispatching to evaluation path")
        _run_eval(cfg)
    else:
        trainer_fn = _TRAINERS.get(algorithm)
        if trainer_fn is None:
            raise ValueError(
                f"Unknown algorithm '{algorithm}'. "
                f"Supported: {', '.join(_TRAINERS)}"
            )
        _log(f"Dispatching to trainer function for algorithm={algorithm}")
        trainer_fn(cfg)
        _log("Trainer function returned to main()")


if __name__ == "__main__":
    import traceback as _tb

    _exit_code = 0
    try:
        main()
    except SystemExit as e:
        _exit_code = e.code if isinstance(e.code, int) else 1
    except Exception:
        print("[Joshua/Isaac] FATAL error during training:", file=sys.stderr)
        _tb.print_exc()
        _exit_code = 1
    finally:
        _log(f"Entering isaac_runner finally block with exit_code={_exit_code}")
        try:
            simulation_app.close()
        except SystemExit:
            pass
        _log("simulation_app.close() completed")
    sys.exit(_exit_code)
