"""Generic agent config builders for Isaac Lab training.

Builds RSL-RL PPO config **classes** from plain kwargs -- no per-task
configclass subclassing needed.

Usage::

    from isaac_lab.rsl_rl_config import build_rsl_rl_cfg

    MyAgentCfg = build_rsl_rl_cfg(
        max_iterations=1000,
        actor_hidden_dims=[256, 128, 64],
        learning_rate=5e-4,
    )
"""

from __future__ import annotations

from isaaclab.utils import configclass
from isaaclab_rl.rsl_rl import (
    RslRlOnPolicyRunnerCfg,
    RslRlPpoActorCriticCfg,
    RslRlPpoAlgorithmCfg,
)


def build_rsl_rl_cfg(
    max_iterations: int = 1000,
    num_steps_per_env: int = 32,
    save_interval: int = 50,
    experiment_name: str = "joshua",
    actor_hidden_dims: list[int] | None = None,
    critic_hidden_dims: list[int] | None = None,
    activation: str = "elu",
    init_noise_std: float = 1.0,
    learning_rate: float = 5e-4,
    entropy_coef: float = 0.0,
    num_learning_epochs: int = 5,
    num_mini_batches: int = 4,
    clip_param: float = 0.2,
    gamma: float = 0.99,
    lam: float = 0.95,
    desired_kl: float = 0.01,
    max_grad_norm: float = 1.0,
    schedule: str = "adaptive",
    seed: int = 42,
) -> type:
    """Build an RSL-RL PPO runner config **class** from keyword arguments.

    Returns a ``@configclass`` class (not an instance).  Isaac Lab's
    ``load_cfg_from_registry`` will instantiate it.
    """
    if actor_hidden_dims is None:
        actor_hidden_dims = [256, 128, 64]
    if critic_hidden_dims is None:
        critic_hidden_dims = [256, 128, 64]

    policy_cfg = RslRlPpoActorCriticCfg(
        init_noise_std=init_noise_std,
        actor_hidden_dims=actor_hidden_dims,
        critic_hidden_dims=critic_hidden_dims,
        activation=activation,
    )
    algorithm_cfg = RslRlPpoAlgorithmCfg(
        value_loss_coef=1.0,
        use_clipped_value_loss=True,
        clip_param=clip_param,
        entropy_coef=entropy_coef,
        num_learning_epochs=num_learning_epochs,
        num_mini_batches=num_mini_batches,
        learning_rate=learning_rate,
        schedule=schedule,
        gamma=gamma,
        lam=lam,
        desired_kl=desired_kl,
        max_grad_norm=max_grad_norm,
    )

    ns = {
        "seed": seed,
        "num_steps_per_env": num_steps_per_env,
        "max_iterations": max_iterations,
        "save_interval": save_interval,
        "experiment_name": experiment_name,
        "empirical_normalization": False,
        "policy": policy_cfg,
        "algorithm": algorithm_cfg,
        "__annotations__": {
            "seed": int,
            "num_steps_per_env": int,
            "max_iterations": int,
            "save_interval": int,
            "experiment_name": str,
            "empirical_normalization": bool,
            "policy": RslRlPpoActorCriticCfg,
            "algorithm": RslRlPpoAlgorithmCfg,
        },
    }
    return configclass(type("AgentCfg", (RslRlOnPolicyRunnerCfg,), ns))
