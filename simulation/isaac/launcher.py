"""Isaac Sim subprocess launcher.

Bridges Joshua's Bazel-managed Python to Isaac Lab's separate Python
environment. Serializes the IsaacSimConfig to JSON, spawns the Isaac
viewer (``simulation/isaac/viewer.py``) as a subprocess, and waits for
it to exit.

IMPORTANT: Bazel sets PYTHONPATH and RUNFILES environment variables that
conflict with Isaac Lab's Python 3.11 + numpy<2. This module sanitizes
the environment before spawning the subprocess so that Isaac Lab uses
its own venv packages, not Bazel's.

Isaac Lab must be pre-installed on the machine. Set one of:
  ISAAC_LAB_PYTHON=/path/to/isaac_venv/bin/python
  ISAAC_LAB_PATH=/path/to/IsaacLab   (uses isaaclab.sh -p)
"""

from __future__ import annotations

import json
import os
import subprocess
import tempfile

import glog

from simulation.proto import simulation_pb2

_ISAAC_VIEWER = os.path.join(os.path.dirname(__file__), "viewer.py")
_MODELS_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "models"))


def _find_isaac_python() -> str:
    """Resolve the Isaac Lab Python interpreter.

    Search order:
      1. ISAAC_LAB_PYTHON env var (explicit path to python binary)
      2. ISAAC_LAB_PATH env var  (uses isaaclab.sh -p)
    """
    explicit = os.environ.get("ISAAC_LAB_PYTHON")
    if explicit:
        if not os.path.isfile(explicit):
            raise FileNotFoundError(f"ISAAC_LAB_PYTHON={explicit} does not exist")
        return explicit

    lab_path = os.environ.get("ISAAC_LAB_PATH")
    if lab_path:
        launcher = os.path.join(lab_path, "isaaclab.sh")
        if os.path.isfile(launcher):
            return launcher
        raise FileNotFoundError(
            f"ISAAC_LAB_PATH={lab_path} but isaaclab.sh not found there"
        )

    raise EnvironmentError(
        "Isaac Lab not found. Set one of:\n"
        "  ISAAC_LAB_PYTHON=/path/to/isaac_venv/bin/python\n"
        "  ISAAC_LAB_PATH=/path/to/IsaacLab"
    )


def _clean_env() -> dict:
    """Build a clean environment for the Isaac Lab subprocess.

    Strips Bazel runfiles paths from PYTHONPATH and removes RUNFILES_*
    variables so Isaac Lab uses its own venv numpy/torch instead of
    Bazel's incompatible Python 3.10 packages.
    """
    env = os.environ.copy()

    for key in list(env):
        if key.startswith("RUNFILES_") or key == "RUNFILES_DIR":
            del env[key]

    bazel_markers = ("bazel-out", "runfiles", ".cache/bazel")
    for path_var in ("PYTHONPATH", "PYTHONHOME"):
        val = env.get(path_var, "")
        if val:
            clean = [
                p
                for p in val.split(os.pathsep)
                if not any(m in p for m in bazel_markers)
            ]
            if clean:
                env[path_var] = os.pathsep.join(clean)
            else:
                del env[path_var]

    env.pop("VIRTUAL_ENV", None)

    return env


def _config_to_json(config: simulation_pb2.IsaacSimConfig) -> dict:
    """Extract viewer params into a plain dict for the subprocess."""
    if not config.usd_filename:
        raise ValueError(
            "simulation.isaac.usd_filename is required for the Isaac Sim backend"
        )

    # usd_filename is relative to simulation/models/, e.g. "ant/ant_isaac.usda".
    usd_path = os.path.join(_MODELS_DIR, config.usd_filename)
    if not os.path.isfile(usd_path):
        raise FileNotFoundError(f"USD model not found: {usd_path}")

    return {
        "usd_path": os.path.abspath(usd_path),
        "init_pos": [config.init_pos_x, config.init_pos_y, config.init_pos_z],
        "init_joint_pos": dict(config.init_joint_pos),
        "actuator_stiffness": config.actuator_stiffness,
        "actuator_damping": config.actuator_damping,
        "sim_dt": config.sim_dt,
    }


def launch(config: simulation_pb2.IsaacSimConfig) -> int:
    """Launch the Isaac Sim viewer as a subprocess; return its exit code."""
    cfg_dict = _config_to_json(config)
    isaac_python = _find_isaac_python()
    viewer_path = os.path.abspath(_ISAAC_VIEWER)

    with tempfile.NamedTemporaryFile(
        mode="w",
        suffix=".json",
        prefix="joshua_isaac_sim_",
        delete=False,
    ) as f:
        json.dump(cfg_dict, f, indent=2)
        cfg_path = f.name

    if isaac_python.endswith("isaaclab.sh"):
        cmd = [isaac_python, "-p", viewer_path, "--config", cfg_path]
    else:
        cmd = [isaac_python, viewer_path, "--config", cfg_path]

    if config.headless:
        cmd.append("--headless")

    glog.info(f"Isaac Sim viewer: {cfg_dict['usd_path']}")
    glog.info(f"  Isaac Python: {isaac_python}")
    glog.info(f"  launching: {' '.join(cmd)}")

    try:
        result = subprocess.run(
            cmd,
            cwd=os.environ.get("ISAAC_LAB_PATH"),
            env=_clean_env(),
        )
        return result.returncode
    except FileNotFoundError as e:
        raise RuntimeError(
            f"Failed to launch Isaac Sim: {e}. "
            f"Ensure Isaac Lab is installed and ISAAC_LAB_PATH is set."
        ) from e
    finally:
        try:
            os.unlink(cfg_path)
        except OSError:
            pass
