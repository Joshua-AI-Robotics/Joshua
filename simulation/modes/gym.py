"""Gymnasium training / evaluation mode.

Launched via the viewer with MODE_GYM config, e.g.:

    bazel run //simulation:viewer -- --config simulation/configs/so_arm100_gym.pbtxt
"""

from __future__ import annotations

import glog
import gymnasium

import simulation.envs.register  # noqa: F401 -- triggers gymnasium.register()
from simulation.proto import simulation_pb2
from simulation.sim_engine import SimEngine

_TASK_TO_ENV_ID = {
    "reach": "SO100Reach-v0",
    "pick_place": "SO100PickPlace-v0",
}

_ALGORITHMS = {
    "ppo": "PPO",
    "sac": "SAC",
    "td3": "TD3",
    "a2c": "A2C",
}


def _make_env(env_id: str, config: simulation_pb2.GymConfig, render: bool):
    kwargs = {}
    if config.frame_skip:
        kwargs["frame_skip"] = config.frame_skip
    if config.image_obs:
        kwargs["image_obs"] = config.image_obs
    if config.camera_name:
        kwargs["camera_name"] = config.camera_name
    if render:
        kwargs["render_mode"] = "human"
    return gymnasium.make(env_id, **kwargs)


def _evaluate(env, model, num_episodes: int) -> None:
    """Run evaluation episodes and log stats."""
    for ep in range(num_episodes):
        obs, _ = env.reset()
        total_reward = 0.0
        steps = 0
        terminated = False
        truncated = False

        while not (terminated or truncated):
            if model is not None:
                action, _ = model.predict(obs, deterministic=True)
            else:
                action = env.action_space.sample()
            obs, reward, terminated, truncated, _ = env.step(action)
            total_reward += reward
            steps += 1

        glog.info(
            f"  eval episode {ep + 1}/{num_episodes}: "
            f"steps={steps}  reward={total_reward:.3f}"
        )


def run(engine: SimEngine, config: simulation_pb2.GymConfig) -> None:
    task_name = config.task or "reach"
    env_id = _TASK_TO_ENV_ID.get(task_name)
    if env_id is None:
        raise ValueError(
            f"Unknown gym task '{task_name}'. "
            f"Available: {', '.join(_TASK_TO_ENV_ID)}"
        )

    total_timesteps = config.total_timesteps
    num_eval = config.num_eval_episodes or 10
    algo_name = (config.algorithm or "ppo").lower()

    if total_timesteps > 0:
        from stable_baselines3 import A2C, PPO, SAC, TD3

        algo_cls_name = _ALGORITHMS.get(algo_name)
        if algo_cls_name is None:
            raise ValueError(
                f"Unknown algorithm '{algo_name}'. "
                f"Available: {', '.join(_ALGORITHMS)}"
            )
        algo_cls = {"PPO": PPO, "SAC": SAC, "TD3": TD3, "A2C": A2C}[algo_cls_name]

        train_env = _make_env(env_id, config, render=config.render_training)
        glog.info(f"Training {algo_cls_name} on {env_id} for {total_timesteps} steps")

        if config.checkpoint_path:
            sb3_model = algo_cls.load(config.checkpoint_path, env=train_env)
            glog.info(f"Resumed from checkpoint: {config.checkpoint_path}")
        else:
            sb3_model = algo_cls("MultiInputPolicy", train_env, verbose=1)

        sb3_model.learn(total_timesteps=total_timesteps)

        save_path = config.save_path or f"{task_name}_{algo_name}"
        sb3_model.save(save_path)
        glog.info(f"Model saved to {save_path}")
        train_env.close()

        glog.info(f"Evaluating trained model for {num_eval} episodes ...")
        eval_env = _make_env(env_id, config, render=True)
        _evaluate(eval_env, sb3_model, num_eval)
        eval_env.close()

    else:
        eval_env = _make_env(env_id, config, render=True)

        sb3_model = None
        if config.checkpoint_path:
            from stable_baselines3 import A2C, PPO, SAC, TD3

            algo_cls_name = _ALGORITHMS.get(algo_name, "PPO")
            algo_cls = {"PPO": PPO, "SAC": SAC, "TD3": TD3, "A2C": A2C}[algo_cls_name]
            sb3_model = algo_cls.load(config.checkpoint_path)
            glog.info(f"Loaded checkpoint: {config.checkpoint_path}")

        policy_label = "trained" if sb3_model else "random"
        glog.info(
            f"Evaluating {policy_label} policy on {env_id} for {num_eval} episodes ..."
        )
        _evaluate(eval_env, sb3_model, num_eval)
        eval_env.close()
