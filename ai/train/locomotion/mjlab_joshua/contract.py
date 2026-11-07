"""Versioned policy interface shared by training and future replay adapters."""

import json
from pathlib import Path

LEGS = ("fl", "fr", "rl", "rr")
JOINT_NAMES = tuple(
    f"{leg}_{joint}" for leg in LEGS for joint in ("hip_yaw", "hip_pitch", "knee_pitch")
)
ACTOR_TERMS = {
    "base_ang_vel": 3,
    "projected_gravity": 3,
    "joint_pos": 12,
    "joint_vel": 12,
    "actions": 12,
    "command": 3,
}
ACTION_SCALE = 0.2
POLICY_DT = 0.02
TASK_ID = "Joshua-Spider-Stand-v0"


def make_contract(design, model_sha256):
    """Use CAD limits and timing without copying robot geometry into code."""
    import math

    limits = [
        [math.radians(v) for v in design["leg"][key]]
        for _ in LEGS
        for key in ("yaw_limits_deg", "hip_limits_deg", "knee_limits_deg")
    ]
    dt = design["simulation"]["timestep_s"]
    decimation = round(POLICY_DT / dt)
    if abs(decimation * dt - POLICY_DT) > 1e-10:
        raise ValueError("CAD timestep must divide the policy period")
    return {
        "schema_version": 1,
        "task_id": TASK_ID,
        "model_sha256": model_sha256,
        "joint_names": list(JOINT_NAMES),
        "joint_limits_rad": limits,
        "home_rad": [0.0] * 12,
        "actor_terms": ACTOR_TERMS,
        "actor_dim": sum(ACTOR_TERMS.values()),
        "critic_dim": 48,
        "action_dim": 12,
        "action_scale_rad": ACTION_SCALE,
        "action_clip": [-1.0, 1.0],
        "previous_action": "clipped normalized action, zero after reset",
        "policy_dt_s": POLICY_DT,
        "physics_dt_s": dt,
        "decimation": decimation,
        "frame": "body x forward, y left, z up; quaternion wxyz",
        "units": "angular velocity rad/s, joint position rad, velocity rad/s",
        "command": ["body vx m/s", "body vy m/s", "yaw rate rad/s"],
        "gravity": "unit world gravity rotated into body frame; upright [0,0,-1]",
        "normalization": "actor/critic running normalization in PPO; export later",
        "target_formula": "home + 0.2 * clip(action,-1,1), then joint limits",
        "velocity_limit_note": "CAD velocity is metadata, not a hard speed clamp",
        "qualification": "Unidentified simulation PD servo; no hardware deployment",
    }


def load_contract(directory):
    """Read the generated contract next to its training model."""
    return json.loads((Path(directory) / "policy_contract.json").read_text())
