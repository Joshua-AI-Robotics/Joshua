"""Write trajectory data as a text-format protobuf (.pbtxt) block."""

from __future__ import annotations

import numpy as np


def write_trajectory_pbtxt(
    path: str,
    topics: list[str],
    timestamps: np.ndarray,
    data: np.ndarray,
    action_type: str,
    node_id: int,
) -> None:
    """Write a ``trajectories { ... }`` block matching TrajectoryPublisher format.

    The output is compatible with ``ros2/trajectory_publisher.py`` and
    can be pasted directly into a robot ``.pbtxt`` config.

    Args:
        path: Output file path.
        topics: ROS2 topic per joint (length = ``data.shape[1]``).
        timestamps: 1-D array of ``timestamp_sec`` per step.
        data: Array of shape ``(num_steps, num_joints)``.
        action_type: One of ``"position"``, ``"torque"``, ``"speed"``.
        node_id: ``TRAJECTORY_PUBLISHER`` node ID in the output.
    """
    lines = [
        "trajectories {",
        "  single_trajectories {",
        "    node {",
        f"      id: {node_id}",
        "      node_type: TRAJECTORY_PUBLISHER",
        "      qos_setting { depth: 10 }",
    ]
    for topic in topics:
        lines.append(
            f'      publishers {{ ros2_data_type: FLOAT32  topic: "{topic}" }}'
        )
    lines.append("    }")
    lines.append("    trajectory {")
    num_steps = data.shape[0]
    for t in range(num_steps):
        for j, topic in enumerate(topics):
            lines.append(
                f"      waypoints {{ timestamp_sec: {timestamps[t]:.6f}  "
                f'topic: "{topic}"  '
                f"action {{ {action_type}: {data[t, j]:.6f} }} }}"
            )
    lines.append("    }")
    lines.append("  }")
    lines.append("}")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"[Joshua/Isaac] Wrote {path}")
