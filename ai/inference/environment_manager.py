"""Build and cache per-model virtual environments."""

from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path

from ai.inference.manifest import repo_root
from ai.inference.proto import model_manifest_pb2

_MARKER = ".joshua_lock_sha256"


def _cache_dir() -> Path:
    return Path(os.environ.get("JOSHUA_ENV_CACHE", "~/.cache/joshua/envs")).expanduser()


def _python_version() -> str:
    return f"{sys.version_info.major}.{sys.version_info.minor}"


def _lock_fingerprint(lock_path: Path) -> str:
    return hashlib.sha256(lock_path.read_bytes()).hexdigest()[:16]


def _resolve_lock_path(manifest: model_manifest_pb2.ModelManifest) -> Path:
    lock = manifest.requirements_lock.strip()
    if not lock:
        raise ValueError(
            f"Model '{manifest.name}' has REEXEC isolation but no requirements_lock."
        )
    lock_path = repo_root() / lock
    if not lock_path.is_file():
        raise FileNotFoundError(f"Requirements lock not found: {lock_path}")
    return lock_path


def _venv_is_ready(venv: Path, lock_fingerprint: str) -> bool:
    marker = venv / _MARKER
    venv_py = venv / "bin" / "python"
    return (
        venv_py.is_file()
        and marker.is_file()
        and marker.read_text(encoding="utf-8").strip() == lock_fingerprint
    )


def _create_venv(venv: Path, python_version: str) -> Path:
    venv.parent.mkdir(parents=True, exist_ok=True)
    if shutil.which("uv"):
        subprocess.check_call(
            [
                "uv",
                "venv",
                "--python",
                python_version,
                str(venv),
            ]
        )
    else:
        subprocess.check_call([sys.executable, "-m", "venv", str(venv)])
    return venv / "bin" / "python"


def _sync_venv(venv_py: Path, lock_path: Path) -> None:
    if shutil.which("uv"):
        subprocess.check_call(
            ["uv", "pip", "sync", "--python", str(venv_py), str(lock_path)]
        )
        return

    subprocess.check_call(
        [
            str(venv_py),
            "-m",
            "pip",
            "install",
            "--ignore-installed",
            "-r",
            str(lock_path),
        ]
    )


def ensure_model_env(manifest: model_manifest_pb2.ModelManifest) -> str:
    """Return the python executable for a cached model virtualenv."""
    lock_path = _resolve_lock_path(manifest)
    lock_fingerprint = _lock_fingerprint(lock_path)
    venv = _cache_dir() / f"{manifest.name}-{lock_fingerprint}"
    venv_py = venv / "bin" / "python"

    if _venv_is_ready(venv, lock_fingerprint):
        return str(venv_py)

    if venv.exists():
        shutil.rmtree(venv)

    python_version = manifest.python_version.strip() or _python_version()
    venv_py = _create_venv(venv, python_version)
    _sync_venv(venv_py, lock_path)
    (venv / _MARKER).write_text(lock_fingerprint, encoding="utf-8")
    return str(venv_py)
