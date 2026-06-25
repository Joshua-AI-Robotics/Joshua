"""ROS Python site-packages paths for rclpy imports.

Paths are defined once here and can be overridden at runtime:

  JOSHUA_ROS_PYTHON_PATH  Colon-separated path list (replaces all presets).
  JOSHUA_ROS_DISTRO         ROS distro preset name (humble, jazzy). Falls back
                            to ROS_DISTRO, then "humble".
"""

from __future__ import annotations

import os
import sys

# Distro-specific ROS Python paths (rclpy, ros message packages).
ROS_DISTRO_PYTHON_PATHS: dict[str, tuple[str, ...]] = {
    "humble": (
        "/opt/ros/humble/lib/python3.10/site-packages",
        "/opt/ros/humble/local/lib/python3.10/dist-packages",
    ),
    "jazzy": (
        "/opt/ros/jazzy/lib/python3.12/site-packages",
        "/opt/ros/jazzy/local/lib/python3.12/dist-packages",
    ),
}

# System Python packages prepended for the Bazel/hermetic base interpreter.
# Excluded from model venvs — Ubuntu's protobuf here breaks generated protos.
SYSTEM_PYTHON_PATHS: tuple[str, ...] = ("/usr/lib/python3/dist-packages",)

_ENV_PYTHON_PATH = "JOSHUA_ROS_PYTHON_PATH"
_ENV_DISTRO = "JOSHUA_ROS_DISTRO"

_PATHS_CONFIGURED = False


def _active_distro() -> str:
    for key in (_ENV_DISTRO, "ROS_DISTRO"):
        value = os.environ.get(key, "").strip().lower()
        if value:
            return value
    return "humble"


def ros_python_paths(*, include_system_paths: bool) -> list[str]:
    """Return existing ROS/system Python paths to add to sys.path."""
    override = os.environ.get(_ENV_PYTHON_PATH, "").strip()
    if override:
        return [path for path in override.split(os.pathsep) if path]

    distro = _active_distro()
    paths = list(ROS_DISTRO_PYTHON_PATHS.get(distro, ROS_DISTRO_PYTHON_PATHS["humble"]))
    if include_system_paths:
        paths = list(SYSTEM_PYTHON_PATHS) + paths
    return paths


def setup_import_paths() -> None:
    """Configure sys.path for rclpy without shadowing model-venv packages."""
    global _PATHS_CONFIGURED
    if _PATHS_CONFIGURED:
        return

    in_model_env = bool(os.environ.get("JOSHUA_MODEL_ENV"))
    paths = ros_python_paths(include_system_paths=not in_model_env)

    if in_model_env:
        for path in paths:
            if os.path.isdir(path) and path not in sys.path:
                sys.path.append(path)
    else:
        for path in reversed(paths):
            if os.path.isdir(path) and path not in sys.path:
                sys.path.insert(0, path)

    _PATHS_CONFIGURED = True
