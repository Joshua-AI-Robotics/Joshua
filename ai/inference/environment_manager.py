"""Build and cache per-model virtual environments."""

from __future__ import annotations

import hashlib
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Optional

from ai.inference.manifest import repo_root
from ai.inference.proto import model_manifest_pb2

_MARKER = ".joshua_lock_sha256"


def _cache_dir() -> Path:
    return Path(os.environ.get("JOSHUA_ENV_CACHE", "~/.cache/joshua/envs")).expanduser()


def _python_version() -> str:
    return f"{sys.version_info.major}.{sys.version_info.minor}"


def _resolve_base_lock_path(python_version: str) -> Path:
    root = repo_root()
    if python_version.startswith("3.12"):
        lock = root / "requirements_3.12.lock"
    else:
        lock = root / "requirements.lock"
    if not lock.is_file():
        raise FileNotFoundError(f"Global requirements lock not found: {lock}")
    return lock


def _resolve_model_lock_path(
    manifest: model_manifest_pb2.ModelManifest,
    python_version: str,
) -> Optional[Path]:
    lock = manifest.requirements_lock.strip()
    if not lock:
        return None
    if python_version.startswith("3.12") and lock.endswith(".lock"):
        lock_312 = f"{lock[:-5]}_3.12.lock"
        lock_312_path = repo_root() / lock_312
        if lock_312_path.is_file():
            return lock_312_path
    lock_path = repo_root() / lock
    if not lock_path.is_file():
        raise FileNotFoundError(f"Model requirements lock not found: {lock_path}")
    return lock_path


def _combined_fingerprint(base_lock: Path, model_lock: Optional[Path]) -> str:
    digest = hashlib.sha256()
    digest.update(base_lock.read_bytes())
    if model_lock is not None:
        digest.update(model_lock.read_bytes())
    return digest.hexdigest()[:16]


def _venv_is_ready(venv: Path, fingerprint: str) -> bool:
    marker = venv / _MARKER
    venv_py = venv / "bin" / "python"
    return (
        venv_py.is_file()
        and marker.is_file()
        and marker.read_text(encoding="utf-8").strip() == fingerprint
    )


def _pip_build_env() -> dict[str, str]:
    """Env for pip/uv when building C extensions into model venvs.

    Bazel's hermetic Python is built with clang, so sysconfig defaults to
    CC=clang. Docker/dev images usually ship gcc (build-essential) only.
    """
    env = os.environ.copy()
    env["CC"] = "gcc"
    env["CXX"] = "g++"
    return env


def _venv_python_executable(python_version: str) -> str:
    """System interpreter for venv creation (not Bazel's hermetic Python)."""
    for candidate in (f"python{python_version}", "python3"):
        path = shutil.which(candidate)
        if path:
            return path
    return sys.executable


def _create_venv(venv: Path, python_version: str) -> Path:
    venv.parent.mkdir(parents=True, exist_ok=True)
    base_py = _venv_python_executable(python_version)
    if shutil.which("uv"):
        subprocess.check_call(
            ["uv", "venv", "--python", base_py, str(venv)],
            env=_pip_build_env(),
        )
    else:
        subprocess.check_call([base_py, "-m", "venv", str(venv)])
    return venv / "bin" / "python"


def _pip_install_locks(venv_py: Path, lock_paths: list[Path]) -> None:
    """Install lock files in order so model-specific pins can override the base."""
    env = _pip_build_env()
    subprocess.check_call(
        [str(venv_py), "-m", "pip", "install", "--upgrade", "pip"],
        env=env,
    )
    if shutil.which("uv"):
        for lock_path in lock_paths:
            subprocess.check_call(
                [
                    "uv",
                    "pip",
                    "install",
                    "--python",
                    str(venv_py),
                    "-r",
                    str(lock_path),
                ],
                env=env,
            )
        return

    for index, lock_path in enumerate(lock_paths):
        cmd = [str(venv_py), "-m", "pip", "install"]
        if index == 0:
            cmd.append("--ignore-installed")
        cmd.extend(["-r", str(lock_path)])
        subprocess.check_call(cmd, env=env)


def _sync_venv(venv_py: Path, base_lock: Path, model_lock: Optional[Path]) -> None:
    lock_paths = [base_lock]
    if model_lock is not None:
        lock_paths.append(model_lock)
    _pip_install_locks(venv_py, lock_paths)


def ensure_model_env(manifest: model_manifest_pb2.ModelManifest) -> str:
    """Return the python executable for a cached model virtualenv."""
    python_version = manifest.python_version.strip() or _python_version()
    base_lock = _resolve_base_lock_path(python_version)
    model_lock = _resolve_model_lock_path(manifest, python_version)
    fingerprint = _combined_fingerprint(base_lock, model_lock)
    venv = _cache_dir() / f"{manifest.name}-{fingerprint}"
    venv_py = venv / "bin" / "python"

    if _venv_is_ready(venv, fingerprint):
        return str(venv_py)

    if venv.exists():
        shutil.rmtree(venv)

    venv_py = _create_venv(venv, python_version)
    _sync_venv(venv_py, base_lock, model_lock)
    (venv / _MARKER).write_text(fingerprint, encoding="utf-8")
    return str(venv_py)
