"""Benchmark, train, and resume the standalone spider walking task."""

import argparse
import hashlib
import json
import math
import os
import platform
import time
from dataclasses import asdict
from datetime import datetime, timezone
from importlib.metadata import version
from pathlib import Path

import torch
from mjlab.envs import ManagerBasedRlEnv
from mjlab.rl import MjlabOnPolicyRunner, RslRlVecEnvWrapper
from mjlab.utils.os import dump_yaml

from .contract import load_contract
from .locomotion import make_walk_cfg, walk_rl_cfg, walking_contract


def verify_asset(asset):
    """Reject a model/contract pair that has drifted since preparation."""
    contract = load_contract(asset)
    digest = hashlib.sha256((asset / "robot.xml").read_bytes()).hexdigest()
    if contract["model_sha256"] != digest:
        raise ValueError("Model hash differs from contract; run prepare/check")
    return contract


def main():
    """Train a bounded run and save its configuration, provenance, and timings."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--num-envs", type=int, default=512)
    parser.add_argument("--iterations", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--resume", type=Path)
    parser.add_argument("--benchmark", action="store_true")
    parser.add_argument("--gait-version", type=int, choices=(1, 2, 3, 4, 5), default=1)
    parser.add_argument("--live", action="store_true", help="Publish live viewer poses")
    parser.add_argument("--live-envs", type=int, default=16)
    parser.add_argument("--live-fps", type=float, default=15)
    args = parser.parse_args()
    if args.num_envs < 4 or args.iterations < 1:
        parser.error("need at least four environments and a positive iteration count")
    if not 1 <= args.live_envs <= 64 or not (
        math.isfinite(args.live_fps) and 1 <= args.live_fps <= 60
    ):
        parser.error("live view needs 1–64 displayed environments and 1–60 fps")
    if not torch.cuda.is_available():
        raise RuntimeError("GPU training requires the configured NVIDIA runtime")
    asset = Path(os.environ["JOSHUA_SPIDER_ASSET"])
    contract = verify_asset(asset)
    contract = walking_contract(contract, args.gait_version)
    root = Path(os.environ["JOSHUA_SPIDER_RL_OUTPUT"])
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    log = root / ("benchmarks" if args.benchmark else "walk") / stamp
    log.mkdir(parents=True)
    cfg = make_walk_cfg(asset, args.num_envs, gait_version=args.gait_version)
    cfg.seed = args.seed
    agent = walk_rl_cfg(args.gait_version)
    agent.seed = args.seed
    agent.max_iterations = args.iterations
    if args.benchmark:
        cfg = make_walk_cfg(
            asset,
            args.num_envs,
            evaluation=True,
            speed=0.03,
            gait_version=args.gait_version,
        )
        cfg.seed = args.seed
    torch.manual_seed(args.seed)
    (log / "policy_contract.json").write_text(json.dumps(contract, indent=2))
    (log / "ppo_config.json").write_text(json.dumps(asdict(agent), indent=2))
    if args.gait_version in (2, 3, 4, 5):
        from .gait import reference_table, settings

        reference = reference_table(str(asset), **settings(args.gait_version))
        (log / "gait_workspace.json").write_text(
            json.dumps({k: v for k, v in reference.items() if k != "table"}, indent=2)
        )
    dump_yaml(log / "env.yaml", asdict(cfg))
    snapshot = log / "source"
    snapshot.mkdir()
    for path in Path(__file__).parent.glob("*.py"):
        (snapshot / path.name).write_bytes(path.read_bytes())
    print(f"RUN_DIR={log}", flush=True)
    env = ManagerBasedRlEnv(cfg=cfg, device="cuda:0")
    stream, status = None, "failed"
    try:
        observations, _ = env.reset()
        for key, dimension in (
            ("actor", contract["actor_dim"]),
            ("critic", contract["critic_dim"]),
        ):
            if observations[key].shape != (args.num_envs, dimension):
                raise RuntimeError(f"{key} observation shape does not match contract")
        if args.live:
            from .live import live_wrapper

            stream = live_wrapper(
                env,
                agent.clip_actions,
                log,
                args.live_envs,
                args.live_fps,
                args.iterations,
            )
        runner = MjlabOnPolicyRunner(
            (
                stream
                if stream is not None
                else RslRlVecEnvWrapper(env, clip_actions=agent.clip_actions)
            ),
            asdict(agent),
            str(log),
            "cuda:0",
        )
        if args.resume:
            previous = json.loads(
                (args.resume.parent / "policy_contract.json").read_text()
            )
            if previous != contract:
                raise ValueError("Resume contract does not match current walking asset")
            runner.load(str(args.resume), map_location="cuda:0")
            runner.current_learning_iteration += 1
            # Apply the restored curriculum before sampling new episode commands.
            env.reset()
        if stream is not None:
            stream.iteration = lambda: runner.current_learning_iteration
            stream.start_iteration = runner.current_learning_iteration
            stream.publish(force=True)
        torch.cuda.synchronize()
        started = time.perf_counter()
        runner.learn(
            num_learning_iterations=args.iterations, init_at_random_ep_len=True
        )
        torch.cuda.synchronize()
        elapsed = time.perf_counter() - started
        for module in (runner.alg.actor, runner.alg.critic):
            if not all(torch.isfinite(p).all() for p in module.parameters()):
                raise RuntimeError("Non-finite policy/value parameters")
        checkpoint = log / f"model_{runner.current_learning_iteration}.pt"
        report = {
            "status": "ok",
            "task_id": contract["task_id"],
            "benchmark": args.benchmark,
            "actor_dim": contract["actor_dim"],
            "critic_dim": contract["critic_dim"],
            "num_envs": args.num_envs,
            "updates_this_run": args.iterations,
            "seed": args.seed,
            "live_view": {
                "enabled": args.live,
                "shown": min(args.live_envs, args.num_envs) if args.live else 0,
                "max_fps": args.live_fps if args.live else None,
            },
            "checkpoint": str(checkpoint),
            "resume": str(args.resume) if args.resume else None,
            "last_iteration": runner.current_learning_iteration,
            "policy_step_counter": env.common_step_counter,
            "training_seconds": elapsed,
            "policy_transitions_per_second": args.num_envs
            * 24
            * args.iterations
            / elapsed,
            "torch_peak_allocated_bytes": torch.cuda.max_memory_allocated(),
            "gpu": torch.cuda.get_device_name(0),
            "python": platform.python_version(),
            "versions": {
                name: version(name)
                for name in ("mjlab", "mujoco", "mujoco-warp", "torch", "warp-lang")
            },
        }
        (log / "training.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2), flush=True)
        status = "completed"
    except KeyboardInterrupt:
        status = "interrupted"
        raise
    finally:
        if stream is not None:
            stream.publish(status, force=True)
        env.close()


if __name__ == "__main__":
    main()
