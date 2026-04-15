"""Core trajectory export logic: data processing, file output, and dispatch."""

from __future__ import annotations

import json
import os

import numpy as np

from trajectory_export.cycle_detection import detect_gait_cycle
from trajectory_export.pbtxt_writer import write_trajectory_pbtxt


def export_trajectory_data(
    positions: np.ndarray,
    actions: np.ndarray,
    step_dt: float,
    cfg: dict,
    joint_names: list[str],
) -> None:
    """Process recorded data, detect cycle, and write output files.

    Produces ``trajectory_position.pbtxt``, ``trajectory_torque.pbtxt``,
    corresponding ``.npy`` files, and a ``trajectory_meta.json``.

    Args:
        positions: Joint positions array ``(num_steps, num_joints)``.
        actions: Raw policy action array ``(num_steps, num_joints)``.
        step_dt: Effective timestep in seconds (``sim_dt * decimation``).
        cfg: JSON config dict with export parameters.
        joint_names: Ordered joint names from the Isaac Lab environment.
    """
    output_dir = cfg.get("output_dir", "")
    if not output_dir:
        output_dir = os.path.join(
            cfg.get("checkpoint_dir", "/tmp/joshua_checkpoints"),
            "trajectory_export",
        )
    os.makedirs(output_dir, exist_ok=True)

    detect_cycle_flag = cfg.get("detect_cycle", False)
    node_id = cfg.get("trajectory_node_id", 1)
    mappings = cfg.get("joint_topic_mappings", [])

    topic_map = {m["joint_name"]: m["topic"] for m in mappings}

    export_indices = []
    export_topics = []
    for i, name in enumerate(joint_names):
        if name in topic_map:
            export_indices.append(i)
            export_topics.append(topic_map[name])
        else:
            print(f"[Joshua/Isaac] WARNING: no topic mapping for joint "
                  f"'{name}', skipping")

    if not export_indices:
        raise ValueError(
            f"No joints matched joint_topic_mappings. "
            f"Available joints: {joint_names}"
        )

    pos_data = positions[:, export_indices]
    act_data = actions[:, export_indices]

    if detect_cycle_flag:
        period = detect_gait_cycle(pos_data)
        if period and period < len(pos_data):
            print(f"[Joshua/Isaac] Detected gait cycle: {period} steps "
                  f"({period * step_dt:.3f}s)")
            pos_data = pos_data[:period]
            act_data = act_data[:period]
        else:
            print("[Joshua/Isaac] Could not detect cycle, using full recording")

    num_steps = len(pos_data)
    timestamps = np.arange(num_steps) * step_dt

    print(f"[Joshua/Isaac] Exporting {num_steps} steps, "
          f"{len(export_topics)} joints, "
          f"duration={timestamps[-1]:.3f}s")

    write_trajectory_pbtxt(
        os.path.join(output_dir, "trajectory_position.pbtxt"),
        export_topics, timestamps, pos_data, "position", node_id,
    )
    write_trajectory_pbtxt(
        os.path.join(output_dir, "trajectory_torque.pbtxt"),
        export_topics, timestamps, act_data, "torque", node_id,
    )

    npy_pos_path = os.path.join(output_dir, "trajectory_position.npy")
    np.save(npy_pos_path, pos_data)
    print(f"[Joshua/Isaac] Wrote {npy_pos_path} {pos_data.shape}")

    npy_act_path = os.path.join(output_dir, "trajectory_torque.npy")
    np.save(npy_act_path, act_data)
    print(f"[Joshua/Isaac] Wrote {npy_act_path} {act_data.shape}")

    meta = {
        "joint_names": [joint_names[i] for i in export_indices],
        "topics": export_topics,
        "num_steps": num_steps,
        "step_dt_s": step_dt,
        "total_duration_s": float(timestamps[-1]),
        "position_units": "observed joint positions (radians)",
        "torque_units": "raw policy action outputs (pre-action-scale)",
    }
    meta_path = os.path.join(output_dir, "trajectory_meta.json")
    with open(meta_path, "w") as f:
        json.dump(meta, f, indent=2)
    print(f"[Joshua/Isaac] Wrote {meta_path}")


def run_trajectory_export(cfg: dict) -> None:
    """Export a trained policy as a constant trajectory.

    Dispatches to the RSL-RL or skrl backend based on
    ``cfg["algorithm"]``.
    """
    algorithm = cfg.get("algorithm", "rsl_rl")
    checkpoint_path = cfg.get("checkpoint_path", "")
    if not checkpoint_path:
        raise ValueError("checkpoint_path is required for trajectory export")
    if not os.path.isfile(checkpoint_path):
        raise FileNotFoundError(f"Checkpoint not found: {checkpoint_path}")

    print(f"[Joshua/Isaac] Trajectory export ({algorithm})")
    print(f"[Joshua/Isaac] Checkpoint: {checkpoint_path}")

    if algorithm == "rsl_rl":
        from trajectory_export.rsl_rl_backend import trajectory_export_rsl_rl
        trajectory_export_rsl_rl(cfg)
    elif algorithm == "skrl":
        from trajectory_export.skrl_backend import trajectory_export_skrl
        trajectory_export_skrl(cfg)
    else:
        raise ValueError(
            f"Unknown algorithm '{algorithm}' for trajectory export")
