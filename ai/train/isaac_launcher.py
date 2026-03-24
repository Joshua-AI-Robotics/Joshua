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
from google.protobuf import json_format

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


def _proto_sub_to_dict(msg) -> dict | None:
    """Convert a proto sub-message to dict, returning None if empty."""
    d = json_format.MessageToDict(msg, preserving_proto_field_name=True)
    return d if d else None


def _config_to_json(config: training_pb2.TrainingConfig) -> dict:
    """Extract training params into a plain dict for the subprocess."""
    rl = config.rl
    d: dict = {
        "task": rl.task,
        "num_envs": rl.num_envs or 4096,
        "max_iterations": rl.max_iterations or 0,
        "total_timesteps": rl.total_timesteps or 0,
        "save_path": rl.save_path or f"{rl.task}_isaac_ppo",
        "algorithm": rl.algorithm or "rsl_rl",
        "render": rl.render,
        "frame_skip": rl.frame_skip or 2,
        "checkpoint_dir": rl.checkpoint_dir or _CHECKPOINT_DIR,
        "save_interval": rl.save_interval or 0,
        "seed": rl.seed or 0,
    }

    for field, key in [
        (rl.ppo, "ppo"),
        (rl.network, "network"),
        (rl.sim_physics, "sim_physics"),
        (rl.termination, "termination"),
        (rl.reset, "reset"),
        (rl.target, "target"),
        (rl.task_config, "task_config"),
    ]:
        sub = _proto_sub_to_dict(field)
        if sub:
            d[key] = sub

    if rl.extra_params:
        d["extra_params"] = dict(rl.extra_params)

    return d


def _launch_subprocess(
    cfg_dict: dict,
    cfg_prefix: str,
    render: bool,
) -> None:
    """Write JSON config and run isaac_runner.py as a subprocess."""
    isaac_python = _find_isaac_python()
    checkpoint_dir = cfg_dict.get("checkpoint_dir", _CHECKPOINT_DIR)

    os.makedirs(checkpoint_dir, exist_ok=True)
    with tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", prefix=cfg_prefix,
        dir=checkpoint_dir, delete=False,
    ) as f:
        json.dump(cfg_dict, f, indent=2)
        cfg_path = f.name

    glog.info(f"Isaac Lab config written to {cfg_path}")
    glog.info(f"  Isaac Python: {isaac_python}")

    runner_path = os.path.abspath(_ISAAC_RUNNER)

    if isaac_python.endswith("isaaclab.sh"):
        cmd = [isaac_python, "-p", runner_path, "--config", cfg_path]
    else:
        cmd = [isaac_python, runner_path, "--config", cfg_path]

    if not render:
        cmd.append("--headless")

    glog.info(f"  launching: {' '.join(cmd)}")

    clean_env = _clean_env()

    try:
        subprocess.run(
            cmd,
            check=True,
            cwd=os.environ.get("ISAAC_LAB_PATH"),
            env=clean_env,
        )
    except subprocess.CalledProcessError as e:
        raise RuntimeError(
            f"Isaac Lab process failed with exit code {e.returncode}"
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


def launch_isaac_training(config: training_pb2.TrainingConfig) -> None:
    """Launch Isaac Lab RL training as a subprocess."""
    cfg_dict = _config_to_json(config)

    glog.info(f"Isaac Lab training: task={cfg_dict['task']}, "
              f"num_envs={cfg_dict['num_envs']}")

    _launch_subprocess(cfg_dict, "joshua_isaac_", cfg_dict["render"])

    save_name = cfg_dict.get("save_path", f"{cfg_dict['task']}_isaac_ppo")
    checkpoint_dir = cfg_dict.get("checkpoint_dir", _CHECKPOINT_DIR)
    meta_path = os.path.join(checkpoint_dir, save_name + "_meta.json")
    if os.path.isfile(meta_path):
        with open(meta_path) as f:
            meta = json.load(f)
        glog.info(f"Isaac Lab training complete. Metadata: {meta_path}")
    else:
        glog.warning(
            f"Training completed but no metadata found at {meta_path}. "
            f"Check Isaac Lab logs for details."
        )


def launch_isaac_eval(config: training_pb2.TrainingConfig) -> None:
    """Launch Isaac Lab evaluation as a subprocess."""
    eval_cfg = config.eval
    cfg_dict: dict = {
        "mode": "eval",
        "task": eval_cfg.task,
        "algorithm": eval_cfg.algorithm or "skrl",
        "model_path": config.model_path,
        "checkpoint_path": eval_cfg.checkpoint_path,
        "num_envs": eval_cfg.num_envs or 32,
        "num_episodes": eval_cfg.num_episodes or 5,
        "render": eval_cfg.render,
        "checkpoint_dir": config.rl.checkpoint_dir or _CHECKPOINT_DIR,
    }

    for field, key in [
        (eval_cfg.task_config, "task_config"),
        (eval_cfg.network, "network"),
        (eval_cfg.sim_physics, "sim_physics"),
        (eval_cfg.termination, "termination"),
        (eval_cfg.reset, "reset"),
        (eval_cfg.ppo, "ppo"),
    ]:
        sub = _proto_sub_to_dict(field)
        if sub:
            cfg_dict[key] = sub

    glog.info(f"Isaac Lab eval: task={cfg_dict.get('task', '')}, "
              f"checkpoint={cfg_dict['checkpoint_path']}")

    _launch_subprocess(cfg_dict, "joshua_isaac_eval_", cfg_dict["render"])
