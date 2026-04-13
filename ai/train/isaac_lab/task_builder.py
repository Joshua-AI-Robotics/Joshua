"""Generic proto-driven task builder for Isaac Lab.

Constructs Isaac Lab environment and agent configs entirely from a JSON
dict (serialised from ``TaskConfig`` proto), eliminating the need for
per-robot Python files.  Uses term registries that map string names to
the factory functions in ``isaac_lab.terms``.

Usage (from ``isaac_runner.py``)::

    from isaac_lab.task_builder import build_task_from_config
    env_cfg_cls, agent_cfg_cls, gym_id = build_task_from_config(cfg)
"""

from __future__ import annotations

import inspect
from typing import Any

import gymnasium as gym

from isaac_lab import terms
from isaac_lab.env_builder import (
    build_env_cfg,
    build_robot_from_usd,
    build_scene_cfg,
    resolve_model_path,
)

# ---------------------------------------------------------------------------
# Term registries: map config string keys -> factory functions
# ---------------------------------------------------------------------------

REWARD_REGISTRY: dict[str, Any] = {
    "progress": terms.progress_reward,
    "alive": terms.is_alive,
    "upright": terms.upright_posture,
    "move_to_target": terms.move_to_target,
    "action_l2": terms.action_l2,
    "energy": terms.power_consumption,
    "joint_pos_limits": terms.joint_pos_limits,
    "lin_vel_z": terms.lin_vel_z_l2,
    "ang_vel_xy": terms.ang_vel_xy_l2,
    "flat_orientation": terms.flat_orientation_l2,
    "action_rate": terms.action_rate_l2,
    "joint_vel": terms.joint_vel_l2,
}

OBSERVATION_REGISTRY: dict[str, Any] = {
    "base_height": terms.base_pos_z,
    "base_lin_vel": terms.base_lin_vel,
    "base_ang_vel": terms.base_ang_vel,
    "base_yaw_roll": terms.base_yaw_roll,
    "base_angle_to_target": terms.base_angle_to_target,
    "base_up_proj": terms.base_up_proj,
    "base_heading_proj": terms.base_heading_proj,
    "joint_pos_norm": terms.joint_pos_normalized,
    "joint_vel_rel": terms.joint_vel_rel,
    "body_forces": terms.body_forces,
    "actions": terms.last_action,
}


def _build_term(factory, term_cfg: dict) -> Any:
    """Call a term factory with the subset of *term_cfg* it accepts."""
    sig = inspect.signature(factory)
    kwargs: dict[str, Any] = {}

    if "weight" in sig.parameters and "weight" in term_cfg:
        kwargs["weight"] = term_cfg["weight"]

    params = term_cfg.get("params", {})
    for name, value in params.items():
        if name in sig.parameters:
            kwargs[name] = value

    if "body_names" in sig.parameters and term_cfg.get("body_names"):
        kwargs["body_names"] = term_cfg["body_names"]

    if "gear_ratio" in sig.parameters and term_cfg.get("gear_ratios"):
        kwargs["gear_ratio"] = term_cfg["gear_ratios"]

    if "target_pos" in sig.parameters and "target_pos" in params:
        pass  # already handled via params loop
    elif "target_pos" in sig.parameters:
        target = term_cfg.get("target_pos")
        if target:
            kwargs["target_pos"] = tuple(target)

    return factory(**kwargs)


def build_task_from_config(
    cfg: dict,
) -> tuple[type, type | None, str]:
    """Build env/agent config classes and gym ID from a JSON task_config.

    Args:
        cfg: Full JSON config dict (must contain ``task_config``).

    Returns:
        ``(EnvCfg_class, AgentCfg_class, gym_id)`` ready for
        ``gym.make(gym_id, cfg=EnvCfg_class())``.
    """

    def _maybe_build_rsl_rl_agent_cfg(task_name: str) -> type | None:
        """Build the optional RSL-RL agent cfg class from top-level ppo/network."""
        algorithm = str(cfg.get("algorithm", "rsl_rl")).lower()
        if algorithm == "skrl":
            # skrl doesn't consume Isaac Lab's rsl_rl_cfg_entry_point.
            return None

        from isaac_lab.rsl_rl_config import build_rsl_rl_cfg

        ppo = cfg.get("ppo", {})
        net = cfg.get("network", {})
        return build_rsl_rl_cfg(
            max_iterations=cfg.get("max_iterations", 0) or 1000,
            num_steps_per_env=ppo.get("num_steps_per_env", 32),
            save_interval=cfg.get("save_interval", 50),
            experiment_name=task_name,
            actor_hidden_dims=list(net.get("actor_hidden_dims", [256, 128, 64])),
            critic_hidden_dims=list(net.get("critic_hidden_dims", [256, 128, 64])),
            activation=net.get("activation", "elu"),
            init_noise_std=net.get("init_noise_std", 1.0),
            learning_rate=ppo.get("learning_rate", 5e-4),
            entropy_coef=ppo.get("entropy_coef", 0.0),
            num_learning_epochs=ppo.get("num_learning_epochs", 5),
            num_mini_batches=ppo.get("num_minibatches", 4),
            clip_param=ppo.get("clip_epsilon", 0.2),
            gamma=ppo.get("gamma", 0.99),
            lam=ppo.get("gae_lambda", 0.95),
            desired_kl=ppo.get("desired_kl", 0.01),
            max_grad_norm=ppo.get("max_grad_norm", 1.0),
            schedule=ppo.get("schedule", "adaptive"),
            seed=cfg.get("seed", 42),
        )

    task_cfg = cfg["task_config"]
    task_name = task_cfg["task_name"]
    gym_id = f"Joshua-{task_name.replace('_', '-').title()}-v0"

    robot_cfg = task_cfg["robot"]
    robot = build_robot_from_usd(
        usd_path=resolve_model_path(robot_cfg["usd_filename"]),
        init_pos=(
            robot_cfg.get("init_pos_x", 0.0),
            robot_cfg.get("init_pos_y", 0.0),
            robot_cfg.get("init_pos_z", 0.3),
        ),
        init_joint_pos=robot_cfg.get("init_joint_pos", {}),
        actuator_stiffness=robot_cfg.get("actuator_stiffness", 0.0),
        actuator_damping=robot_cfg.get("actuator_damping", 0.0),
    )

    physics_cfg = cfg.get("sim_physics", {})
    num_envs = cfg.get("num_envs", 4096)
    scene = build_scene_cfg(
        robot=robot,
        num_envs=num_envs,
        env_spacing=physics_cfg.get("env_spacing", 5.0),
        terrain_friction=physics_cfg.get("terrain_friction", 1.0),
    )

    action_scale = physics_cfg.get("action_scale", 1.0)
    actions = terms.joint_effort_actions(scale=action_scale)

    rewards: dict[str, Any] = {}
    for name, term_cfg in task_cfg.get("rewards", {}).items():
        factory = REWARD_REGISTRY.get(name)
        if factory is None:
            raise ValueError(
                f"Unknown reward term '{name}'. "
                f"Known: {sorted(REWARD_REGISTRY)}"
            )
        rewards[name] = _build_term(factory, term_cfg)

    observations: dict[str, Any] = {}
    for name, term_cfg in task_cfg.get("observations", {}).items():
        factory = OBSERVATION_REGISTRY.get(name)
        if factory is None:
            raise ValueError(
                f"Unknown observation term '{name}'. "
                f"Known: {sorted(OBSERVATION_REGISTRY)}"
            )
        observations[name] = _build_term(factory, term_cfg)

    termination_cfg = cfg.get("termination", {})
    terminations = {
        "time_out": terms.time_out(),
        "torso_height": terms.root_height_below(
            minimum_height=termination_cfg.get("min_root_height", 0.1),
        ),
    }

    reset_cfg = cfg.get("reset", {})
    events = {
        "reset_base": terms.reset_root_state(),
        "reset_robot_joints": terms.reset_joints_by_offset(
            pos_range=(
                reset_cfg.get("joint_pos_range_min", -0.2),
                reset_cfg.get("joint_pos_range_max", 0.2),
            ),
            vel_range=(
                reset_cfg.get("joint_vel_range_min", -0.1),
                reset_cfg.get("joint_vel_range_max", 0.1),
            ),
        ),
    }

    env_cfg_cls = build_env_cfg(
        scene_cfg=scene,
        actions_cfg=actions,
        rewards=rewards,
        observations=observations,
        terminations=terminations,
        events=events,
        decimation=int(physics_cfg.get("decimation", 2)),
        episode_length_s=physics_cfg.get("episode_length_s", 16.0),
        sim_dt=physics_cfg.get("sim_dt", 1.0 / 120.0),
        bounce_threshold_velocity=physics_cfg.get("bounce_threshold_velocity", 0.2),
        physics_static_friction=physics_cfg.get("terrain_friction", 1.0),
        physics_dynamic_friction=physics_cfg.get("terrain_friction", 1.0),
        physics_restitution=0.0,
    )

    agent_cfg_cls = _maybe_build_rsl_rl_agent_cfg(task_name)

    gym_kwargs = {
        "env_cfg_entry_point": env_cfg_cls,
    }
    if agent_cfg_cls is not None:
        gym_kwargs["rsl_rl_cfg_entry_point"] = agent_cfg_cls

    gym.register(
        id=gym_id,
        entry_point="isaaclab.envs:ManagerBasedRLEnv",
        disable_env_checker=True,
        kwargs=gym_kwargs,
    )

    return env_cfg_cls, agent_cfg_cls, gym_id
