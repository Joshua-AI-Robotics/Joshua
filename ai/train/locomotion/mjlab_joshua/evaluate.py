"""Seeded walking evaluation with sticky failures, progress gates, and video."""

import argparse
import hashlib
import json
import os
from dataclasses import asdict
from datetime import datetime, timezone
from pathlib import Path

import imageio.v2 as imageio
import numpy as np
import torch
from mjlab.envs import ManagerBasedRlEnv
from mjlab.managers import EventTermCfg
from mjlab.rl import MjlabOnPolicyRunner, RslRlVecEnvWrapper
from mjlab.utils.lab_api.math import quat_from_euler_xyz
from tensordict import TensorDict

from .gait import leg_phase
from .locomotion import (
    feet_state,
    gait_version_from_contract,
    make_walk_cfg,
    walk_rl_cfg,
    walking_contract,
)
from .train import verify_asset


def reset_cases(env, env_ids, seed_base):
    """Give each case its own reproducible seed, independent of the batch RNG."""
    if env_ids is None:
        env_ids = torch.arange(env.num_envs, device=env.device)
    samples = np.array(
        [
            np.random.default_rng(seed_base + i).uniform(-1, 1, size=15)
            for i in env_ids.tolist()
        ]
    )
    values = torch.as_tensor(samples, dtype=torch.float32, device=env.device)
    robot = env.scene["robot"]
    root = robot.data.default_root_state[env_ids].clone()
    root[:, :3] += env.scene.env_origins[env_ids]
    angles = values[:, :3] * torch.tensor([0.02, 0.02, 0.05], device=env.device)
    root[:, 3:7] = quat_from_euler_xyz(*angles.unbind(-1))
    robot.write_root_state_to_sim(root, env_ids=env_ids)
    position = robot.data.default_joint_pos[env_ids] + values[:, 3:] * 0.01
    robot.write_joint_state_to_sim(
        position, torch.zeros_like(position), env_ids=env_ids
    )


def successful(survived, rmse, forward_m, required_m, tolerance=0.02):
    """Standing still or surviving repeated resets must never pass walking."""
    return survived & (rmse <= tolerance) & (forward_m >= required_m)


def gait_successful(walking, air, clearance, support, samples):
    """Every leg must step; an average over legs can hide a permanently dragged foot."""
    return (
        walking
        & (air >= 0.5).all(-1)
        & (clearance >= 0.005).all(-1)
        & (support >= 0.8)
        & (samples > 0).all(-1)
    )


def qualifies(successes, episodes, seconds, speed, sampled=False):
    """A short smoke or easier command cannot qualify the full walking protocol."""
    return (
        episodes >= 100
        and seconds >= 30
        and abs(speed - 0.05) < 1e-9
        and successes * 10 >= episodes * 9
        and not sampled
    )


def main():
    """Evaluate one fixed command per episode, retaining every case's first failure."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--checkpoint", type=Path, required=True)
    parser.add_argument("--episodes", type=int, default=100)
    parser.add_argument("--seconds", type=float, default=30)
    parser.add_argument("--speed", type=float, default=0.05)
    parser.add_argument("--seed-base", type=int, default=10000)
    parser.add_argument("--video", action="store_true")
    parser.add_argument("--video-seconds", type=float, default=10)
    parser.add_argument("--require-pass", action="store_true")
    parser.add_argument("--require-gait-pass", action="store_true")
    parser.add_argument(
        "--zero-residual", action="store_true", help="Evaluate the v5 gait prior alone"
    )
    parser.add_argument(
        "--sample-actions",
        action="store_true",
        help="Diagnostic only; cannot qualify the deterministic policy",
    )
    args = parser.parse_args()
    if (
        args.episodes < 1
        or args.seconds <= 2
        or args.speed <= 0
        or args.video_seconds <= 0
    ):
        parser.error(
            "episodes/speed must be positive, duration >2 s, and video duration >0"
        )
    asset = Path(os.environ["JOSHUA_SPIDER_ASSET"])
    saved = json.loads((args.checkpoint.parent / "policy_contract.json").read_text())
    gait_version = gait_version_from_contract(saved)
    if args.zero_residual and (gait_version != 5 or args.sample_actions):
        parser.error("zero-residual ablation requires v5 and deterministic actions")
    contract = walking_contract(verify_asset(asset), gait_version)
    if saved != contract:
        raise ValueError(
            "Evaluation checkpoint contract differs from the current asset"
        )
    cfg = make_walk_cfg(
        asset,
        args.episodes,
        evaluation=True,
        speed=args.speed,
        gait_version=gait_version,
    )
    cfg.seed = args.seed_base
    cfg.episode_length_s = args.seconds + 1
    cfg.events = {
        "cases": EventTermCfg(
            func=reset_cases, mode="reset", params={"seed_base": args.seed_base}
        )
    }
    cfg.viewer.width, cfg.viewer.height = 960, 720
    cfg.viewer.max_extra_envs = 0
    cfg.viewer.azimuth, cfg.viewer.elevation = 135, -25
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    out = args.checkpoint.parent / "evaluations" / stamp
    out.mkdir(parents=True)
    print(f"EVAL_DIR={out}", flush=True)
    env = ManagerBasedRlEnv(
        cfg=cfg,
        device="cuda:0",
        render_mode="rgb_array" if args.video else None,
    )
    if args.video:
        # Group 3 collision primitives are hidden by MuJoCo's default renderer.
        # Change the CPU render model only; the GPU physics model is unchanged.
        model = env.sim.mj_model
        model.geom_group[model.geom_group == 3] = 2
        model.site_rgba[:, 3] = 0
    writer = None
    try:
        agent = walk_rl_cfg(gait_version)
        runner = MjlabOnPolicyRunner(
            RslRlVecEnvWrapper(env, clip_actions=1.0), asdict(agent), None, "cuda:0"
        )
        runner.load(str(args.checkpoint), map_location="cuda:0")
        policy = runner.get_inference_policy(device="cuda:0")
        obs, _ = env.reset(seed=args.seed_base)
        robot = env.scene["robot"]
        n, device = args.episodes, env.device
        alive = torch.ones(n, dtype=torch.bool, device=device)
        error_sum, slip_sum, count = [torch.zeros(n, device=device) for _ in range(3)]
        saturation_sum = torch.zeros(n, device=device)
        target_saturation_sum = torch.zeros(n, device=device)
        action_term = env.action_manager.get_term("joint_pos")
        velocity_sum = torch.zeros((n, 2), device=device)
        heading_drift = torch.zeros(n, device=device)
        previous_heading = robot.data.heading_w.clone()
        max_effort, max_speed, peak_clearance = [
            torch.zeros(n, device=device) for _ in range(3)
        ]
        start = robot.data.root_link_pos_w[:, 0].clone()
        forward = torch.zeros(n, device=device)
        first_failure = torch.full((n,), -1, dtype=torch.long, device=device)
        swing_total, swing_air, mid_total, mid_height = [
            torch.zeros((n, 4), device=device) for _ in range(4)
        ]
        support_total, grounded_total = [
            torch.zeros(n, device=device) for _ in range(2)
        ]
        failures = {
            name: torch.zeros(n, dtype=torch.bool, device=device)
            for name in cfg.terminations
        }
        if args.video:
            writer = imageio.get_writer(
                out / f"case_{args.seed_base}.mp4", fps=25, codec="libx264"
            )
        with torch.inference_mode():
            for step in range(round(args.seconds / env.step_dt)):
                raw_actions = policy(
                    TensorDict(obs, batch_size=[n]),
                    stochastic_output=args.sample_actions,
                )
                if args.zero_residual:
                    raw_actions.zero_()
                actions = raw_actions.clamp(-1, 1)
                if not torch.isfinite(raw_actions).all():
                    raise RuntimeError("Non-finite evaluation action")
                obs, _, done, timeout, _ = env.step(actions)
                failed = alive & (done | timeout)
                first_failure[failed] = step
                for name in failures:
                    failures[name] |= failed & env.termination_manager.get_term(name)
                alive &= ~(done | timeout)
                valid = alive & (step >= round(2 / env.step_dt))
                velocity = robot.data.root_link_lin_vel_b[:, :2]
                target = torch.tensor([args.speed, 0], device=device)
                err = (velocity - target).square().sum(-1)
                contact, clearance, foot_speed = feet_state(env)
                phase = leg_phase(env)
                swing = (phase < 0.25) & valid[:, None]
                mid = (phase >= 0.075) & (phase < 0.175) & valid[:, None]
                swing_total += swing
                swing_air += swing & ~contact
                mid_total += mid
                mid_height += clearance * mid
                support_total += (contact.sum(-1) >= 3) & valid
                grounded_total += contact.all(-1) & valid
                slip = (foot_speed * contact).sum(-1) / contact.sum(-1).clamp(min=1)
                error_sum += err * valid
                slip_sum += slip * valid
                count += valid
                velocity_sum += velocity * valid[:, None]
                heading = robot.data.heading_w
                heading_delta = (heading - previous_heading + np.pi) % (
                    2 * np.pi
                ) - np.pi
                heading_drift += heading_delta * alive
                previous_heading = heading.clone()
                saturation_sum += (raw_actions.abs() >= 1).float().mean(-1) * valid
                if hasattr(action_term, "target_saturated"):
                    target_saturation_sum += (
                        action_term.target_saturated.float().mean(-1) * valid
                    )
                forward = torch.where(
                    alive, robot.data.root_link_pos_w[:, 0] - start, forward
                )
                max_effort = torch.maximum(
                    max_effort, robot.data.actuator_force.abs().amax(-1) * alive
                )
                max_speed = torch.maximum(
                    max_speed, robot.data.joint_vel.abs().amax(-1) * alive
                )
                peak_clearance = torch.maximum(
                    peak_clearance, clearance.clamp(min=0).amax(-1) * alive
                )
                if (
                    writer
                    and step % 2 == 0
                    and step * env.step_dt < args.video_seconds
                    and alive[0]
                ):
                    frame = env.render()
                    writer.append_data(frame)
                    if step == 0 or step == 50:
                        imageio.imwrite(out / "preview.png", frame)
        rmse = torch.sqrt(error_sum / count.clamp(min=1))
        slip = slip_sum / count.clamp(min=1)
        required = args.speed * (args.seconds - 2) * 0.6
        passed = successful(alive, rmse, forward, required)
        air_fraction = swing_air / swing_total.clamp(min=1)
        mid_clearance = mid_height / mid_total.clamp(min=1)
        support_fraction = support_total / count.clamp(min=1)
        gait_passed = gait_successful(
            passed, air_fraction, mid_clearance, support_fraction, mid_total
        )
        rows = [
            {
                "seed": args.seed_base + i,
                "survived": bool(alive[i]),
                "success": bool(passed[i]),
                "velocity_rmse_m_s": float(rmse[i]),
                "forward_m": float(forward[i]),
                "mean_stance_slip_m_s": float(slip[i]),
                "mean_body_velocity_xy_m_s": (
                    velocity_sum[i] / count[i].clamp(min=1)
                ).tolist(),
                "heading_drift_rad": float(heading_drift[i]),
                "action_saturation_fraction": float(
                    saturation_sum[i] / count[i].clamp(min=1)
                ),
                "target_saturation_fraction": (
                    float(target_saturation_sum[i] / count[i].clamp(min=1))
                    if hasattr(action_term, "target_saturated")
                    else None
                ),
                "peak_joint_effort_Nm": float(max_effort[i]),
                "peak_joint_velocity_rad_s": float(max_speed[i]),
                "peak_foot_clearance_m": float(peak_clearance[i]),
                "swing_air_fraction_by_leg": air_fraction[i].tolist(),
                "mid_swing_clearance_m_by_leg": mid_clearance[i].tolist(),
                "three_foot_support_fraction": float(support_fraction[i]),
                "all_feet_grounded_fraction": float(
                    grounded_total[i] / count[i].clamp(min=1)
                ),
                "gait_success": bool(gait_passed[i]),
                "first_failure_step": int(first_failure[i]),
                "failure_reasons": [
                    key for key, values in failures.items() if values[i]
                ],
            }
            for i in range(n)
        ]
        report = {
            "task_id": contract["task_id"],
            "checkpoint": str(args.checkpoint),
            "checkpoint_sha256": hashlib.sha256(
                args.checkpoint.read_bytes()
            ).hexdigest(),
            "model_sha256": contract["model_sha256"],
            "episodes": n,
            "seconds": args.seconds,
            "command_m_s": args.speed,
            "survived": int(alive.sum()),
            "successful": int(passed.sum()),
            "pass_fraction": float(passed.float().mean()),
            "policy_mode": (
                "generator_only"
                if args.zero_residual
                else "sampled_diagnostic" if args.sample_actions else "deterministic"
            ),
            "gate_passed": qualifies(
                int(passed.sum()), n, args.seconds, args.speed, args.sample_actions
            ),
            "criteria": {
                "velocity_rmse_max_m_s": 0.02,
                "minimum_forward_m": required,
                "no_reset_or_failure": True,
                "warmup_excluded_s": 2,
            },
            "gait_successful": int(gait_passed.sum()),
            "gait_gate_passed": qualifies(
                int(gait_passed.sum()), n, args.seconds, args.speed, args.sample_actions
            ),
            "gait_criteria": {
                "leg_order": ["fl", "fr", "rl", "rr"],
                "swing_air_fraction_min_each_leg": 0.5,
                "mid_swing_clearance_min_m_each_leg": 0.005,
                "three_foot_support_fraction_min": 0.8,
                "requires_existing_walking_gate": True,
            },
            "survivor_velocity_rmse_mean_m_s": (
                float(rmse[alive].mean()) if alive.any() else None
            ),
            "cases": rows,
        }
        (out / "evaluation.json").write_text(json.dumps(report, indent=2) + "\n")
        print(
            json.dumps({k: v for k, v in report.items() if k != "cases"}, indent=2),
            flush=True,
        )
        if args.require_pass and not report["gate_passed"]:
            raise SystemExit(1)
        if args.require_gait_pass and not report["gait_gate_passed"]:
            raise SystemExit(1)
    finally:
        if writer:
            writer.close()
        env.close()


if __name__ == "__main__":
    main()
