"""Check the batched task and run five PPO updates; this does not train a gait."""

import argparse
import hashlib
import json
import os
import platform
from dataclasses import asdict
from datetime import datetime, timezone
from importlib.metadata import version
from pathlib import Path

import mujoco
import torch
from mjlab.envs import ManagerBasedRlEnv
from mjlab.rl import MjlabOnPolicyRunner, RslRlVecEnvWrapper

from .contract import JOINT_NAMES, load_contract
from .task import make_env_cfg, make_rl_cfg


def check_observations(obs, count):
    """Reject shape drift and NaNs before optimization starts."""
    for key, width in (("actor", 45), ("critic", 48)):
        assert tuple(obs[key].shape) == (count, width), (key, obs[key].shape)
        assert torch.isfinite(obs[key]).all(), key


def main():
    """Run a bounded, local-only smoke with machine-readable evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--num-envs", type=int, default=64)
    parser.add_argument("--device", choices=("cpu", "cuda:0"), default="cuda:0")
    args = parser.parse_args()
    if args.num_envs < 4:
        parser.error("use at least four environments for PPO minibatches")
    if args.device.startswith("cuda") and not torch.cuda.is_available():
        raise RuntimeError("CUDA unavailable; check the NVIDIA Container Toolkit")
    asset = Path(os.environ["JOSHUA_SPIDER_ASSET"])
    contract = load_contract(asset)
    digest = hashlib.sha256((asset / "robot.xml").read_bytes()).hexdigest()
    assert digest == contract["model_sha256"], "Regenerate the asset/contract together"
    torch.manual_seed(42)
    cfg = make_env_cfg(asset, args.num_envs)
    cfg.seed = 42
    env = ManagerBasedRlEnv(cfg=cfg, device=args.device)
    try:
        assert tuple(env.scene["robot"].joint_names) == JOINT_NAMES
        obs, _ = env.reset()
        check_observations(obs, args.num_envs)
        zero = torch.zeros((args.num_envs, 12), device=args.device)
        # Three seconds of held home target is a prerequisite for learning.
        for step in range(150):
            obs, reward, terminated, truncated, _ = env.step(zero)
            check_observations(obs, args.num_envs)
            assert torch.isfinite(reward).all()
            assert not (terminated | truncated).any(), (
                "Home pose failed to stand",
                step,
                {
                    name: env.termination_manager.get_term(name).tolist()
                    for name in cfg.terminations
                },
                env.scene["body_ground"].data.found.tolist(),
            )
        robot = env.scene["robot"]
        torch.testing.assert_close(
            obs["actor"][:, 6:18], robot.data.joint_pos - robot.data.default_joint_pos
        )
        torch.testing.assert_close(obs["actor"][:, 3:6], robot.data.projected_gravity_b)
        min_height = float(robot.data.root_link_pos_w[:, 2].min())
        upright = float((-robot.data.projected_gravity_b[:, 2]).min())
        assert min_height > 0.04 and upright > 0.9, (min_height, upright)
        # Exercise saturation and reset feedback, independently of the PPO wrapper.
        obs, *_ = env.step(torch.full_like(zero, 10.0))
        term = env.action_manager.get_term("joint_pos")
        assert tuple(term.target_names) == JOINT_NAMES
        assert torch.all(term.raw_action == 1)
        assert torch.all(term._processed_actions <= 0.2 + 1e-6)
        assert torch.all(obs["actor"][:, 30:42] == 1)
        env.step(torch.full_like(zero, -10.0))
        assert torch.all(term.raw_action == -1)
        assert torch.all(term._processed_actions >= -0.2 - 1e-6)
        obs, _ = env.reset()
        check_observations(obs, args.num_envs)
        assert torch.all(obs["actor"][:, 30:42] == 0), "Previous action leaked on reset"
        rl_cfg = make_rl_cfg()
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
        log_dir = Path(os.environ["JOSHUA_SPIDER_RL_OUTPUT"]) / "runs" / stamp
        log_dir.mkdir(parents=True)
        (log_dir / "policy_contract.json").write_text(json.dumps(contract, indent=2))
        (log_dir / "ppo_config.json").write_text(json.dumps(asdict(rl_cfg), indent=2))
        wrapper = RslRlVecEnvWrapper(env, clip_actions=rl_cfg.clip_actions)
        runner = MjlabOnPolicyRunner(wrapper, asdict(rl_cfg), str(log_dir), args.device)
        runner.learn(num_learning_iterations=5, init_at_random_ep_len=False)
        assert all(torch.isfinite(p).all() for p in runner.alg.actor.parameters())
        assert all(torch.isfinite(p).all() for p in runner.alg.critic.parameters())
        report = {
            "status": "ok",
            "scope": "standing and PPO smoke, no gait qualification",
            "num_envs": args.num_envs,
            "ppo_iterations": 5,
            "seed": 42,
            "actor_dim": 45,
            "critic_dim": 48,
            "action_dim": 12,
            "standing_3s_min_height_m": min_height,
            "standing_3s_min_upright_cosine": upright,
            "device": args.device,
            "gpu": torch.cuda.get_device_name(0) if args.device != "cpu" else None,
            "torch": torch.__version__,
            "mujoco": mujoco.__version__,
            "mjlab": version("mjlab"),
            "mujoco_warp": version("mujoco-warp"),
            "warp": version("warp-lang"),
            "python": platform.python_version(),
            "model_sha256": digest,
            "requirements_sha256": hashlib.sha256(
                (Path(__file__).parents[1] / "requirements.lock").read_bytes()
            ).hexdigest(),
            "cuda_peak_allocated_bytes": (
                torch.cuda.max_memory_allocated() if args.device != "cpu" else None
            ),
        }
        (log_dir / "smoke.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))
        print(f"Evidence: {log_dir}")
    finally:
        env.close()


if __name__ == "__main__":
    main()
