"""Isaac Lab subprocess launcher.

Bridges Joshua's Bazel-managed training pipeline to Isaac Lab's separate
Python environment.  Serializes the TrainingConfig to JSON, spawns the
Isaac Lab runner as a subprocess, and collects results.

IMPORTANT: Bazel sets PYTHONPATH and RUNFILES environment variables that
conflict with Isaac Lab's Python 3.11 + numpy<2.  This module
sanitizes the environment before spawning the subprocess so that Isaac
Lab uses its own venv packages, not Bazel's.

Isaac Lab must be pre-installed on the machine.  Set the ISAAC_LAB_PATH
environment variable to the Isaac Lab root (e.g. /home/user/IsaacLab).
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from typing import Optional

import glog

from ai.proto import training_pb2

_ISAAC_RUNNER = os.path.join(
    os.path.dirname(__file__), "isaac_runner.py"
)

_CHECKPOINT_DIR = "/tmp/joshua_checkpoints"


def _find_isaac_python() -> str:
    """Resolve the Isaac Lab Python interpreter.

    Search order:
      1. ISAAC_LAB_PYTHON env var (explicit path to python binary)
      2. ISAAC_LAB_PATH env var  (uses isaaclab.sh -p)
    """
    explicit = os.environ.get("ISAAC_LAB_PYTHON")
    if explicit:
        if not os.path.isfile(explicit):
            raise FileNotFoundError(
                f"ISAAC_LAB_PYTHON={explicit} does not exist"
            )
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

    Strips Bazel runfiles paths from PYTHONPATH and removes
    RUNFILES_* variables so Isaac Lab uses its own venv numpy/torch
    instead of Bazel's incompatible Python 3.10 packages.
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
                p for p in val.split(os.pathsep)
                if not any(m in p for m in bazel_markers)
            ]
            if clean:
                env[path_var] = os.pathsep.join(clean)
            else:
                del env[path_var]

    env.pop("VIRTUAL_ENV", None)

    return env


def _config_to_json(config: training_pb2.TrainingConfig) -> dict:
    """Extract training params into a plain dict for the subprocess."""
    rl = config.rl
    return {
        "task": rl.task,
        "num_envs": rl.num_envs or 4096,
        "max_iterations": rl.max_iterations or 0,
        "total_timesteps": rl.total_timesteps or 0,
        "save_path": rl.save_path or f"{rl.task}_isaac_ppo",
        "algorithm": rl.algorithm or "rsl_rl",
        "render": rl.render,
        "frame_skip": rl.frame_skip or 2,
        "checkpoint_dir": _CHECKPOINT_DIR,
    }


def launch_isaac_training(config: training_pb2.TrainingConfig) -> None:
    """Launch Isaac Lab RL training as a subprocess."""
    isaac_python = _find_isaac_python()
    cfg_dict = _config_to_json(config)

    os.makedirs(_CHECKPOINT_DIR, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", prefix="joshua_isaac_",
        dir=_CHECKPOINT_DIR, delete=False,
    ) as f:
        json.dump(cfg_dict, f, indent=2)
        cfg_path = f.name

    glog.info(f"Isaac Lab training: task={cfg_dict['task']}, "
              f"num_envs={cfg_dict['num_envs']}")
    glog.info(f"  config written to {cfg_path}")
    glog.info(f"  Isaac Python: {isaac_python}")

    runner_path = os.path.abspath(_ISAAC_RUNNER)

    if isaac_python.endswith("isaaclab.sh"):
        cmd = [isaac_python, "-p", runner_path, "--config", cfg_path]
    else:
        cmd = [isaac_python, runner_path, "--config", cfg_path]

    if not cfg_dict["render"]:
        cmd.append("--headless")

    glog.info(f"  launching: {' '.join(cmd)}")

    clean_env = _clean_env()

    try:
        result = subprocess.run(
            cmd,
            check=True,
            cwd=os.environ.get("ISAAC_LAB_PATH"),
            env=clean_env,
        )
    except subprocess.CalledProcessError as e:
        raise RuntimeError(
            f"Isaac Lab training failed with exit code {e.returncode}"
        ) from e
    except FileNotFoundError as e:
        raise RuntimeError(
            f"Failed to launch Isaac Lab: {e}. "
            f"Ensure Isaac Lab is installed and ISAAC_LAB_PATH is set."
        ) from e
    finally:
        try:
            os.unlink(cfg_path)
        except OSError:
            pass

    meta_path = os.path.join(
        _CHECKPOINT_DIR, cfg_dict["save_path"] + "_meta.json"
    )
    if os.path.isfile(meta_path):
        with open(meta_path) as f:
            meta = json.load(f)
        glog.info(f"Isaac Lab training complete. Checkpoint metadata: {meta_path}")
    else:
        glog.warning(
            f"Training completed but no metadata found at {meta_path}. "
            f"Check Isaac Lab logs for details."
        )


def launch_isaac_eval(config: training_pb2.TrainingConfig) -> None:
    """Launch Isaac Lab evaluation as a subprocess."""
    isaac_python = _find_isaac_python()

    eval_cfg = config.eval
    cfg_dict = {
        "mode": "eval",
        "task": eval_cfg.task,
        "algorithm": eval_cfg.algorithm or "skrl",
        "model_path": config.model_path,
        "checkpoint_path": eval_cfg.checkpoint_path,
        "num_envs": eval_cfg.num_envs or 32,
        "num_episodes": eval_cfg.num_episodes or 5,
        "render": eval_cfg.render,
        "checkpoint_dir": _CHECKPOINT_DIR,
    }

    os.makedirs(_CHECKPOINT_DIR, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", prefix="joshua_isaac_eval_",
        dir=_CHECKPOINT_DIR, delete=False,
    ) as f:
        json.dump(cfg_dict, f, indent=2)
        cfg_path = f.name

    glog.info(f"Isaac Lab eval: task={cfg_dict['task']}, "
              f"checkpoint={cfg_dict['checkpoint_path']}")

    runner_path = os.path.abspath(_ISAAC_RUNNER)

    if isaac_python.endswith("isaaclab.sh"):
        cmd = [isaac_python, "-p", runner_path, "--config", cfg_path]
    else:
        cmd = [isaac_python, runner_path, "--config", cfg_path]

    if not cfg_dict["render"]:
        cmd.append("--headless")

    clean_env = _clean_env()

    try:
        subprocess.run(
            cmd, check=True,
            cwd=os.environ.get("ISAAC_LAB_PATH"),
            env=clean_env,
        )
    except subprocess.CalledProcessError as e:
        raise RuntimeError(
            f"Isaac Lab eval failed with exit code {e.returncode}"
        ) from e
    finally:
        try:
            os.unlink(cfg_path)
        except OSError:
            pass
