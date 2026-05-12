"""Trajectory export package for Joshua.

Converts a trained Isaac Lab RL policy into a constant trajectory
(.pbtxt waypoints + .npy arrays) that can be replayed open-loop
on a real robot via ``trajectory_publisher.py``.

This package runs inside Isaac Lab's Python environment (not Bazel).
"""

from trajectory_export.exporter import run_trajectory_export  # noqa: F401
