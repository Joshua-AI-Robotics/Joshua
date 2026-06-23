"""Discover and load per-model manifests."""

from __future__ import annotations

import os
from functools import lru_cache
from importlib import import_module
from pathlib import Path
from typing import Type

from google.protobuf import text_format

from ai.inference.adapter import InferenceAdapter
from ai.inference.proto import model_manifest_pb2
from ai.proto import ai_model_pb2


def _workspace_candidates() -> list[Path]:
    """Candidate workspace roots, runfiles first when RUNFILES_DIR is set."""
    candidates: list[Path] = []
    runfiles = os.environ.get("RUNFILES_DIR", "").strip()
    if runfiles:
        runfiles_path = Path(runfiles)
        candidates.extend(
            [
                runfiles_path / "_main",
                runfiles_path / "project_joshua",
                runfiles_path,
            ]
        )
    candidates.append(Path(__file__).resolve().parents[2])
    return candidates


def repo_root() -> Path:
    """Return the Joshua workspace root (Bazel runfiles or source tree)."""
    for candidate in _workspace_candidates():
        if (candidate / "ai" / "proto" / "ai_model_pb2.py").is_file():
            return candidate
    for candidate in _workspace_candidates():
        if (candidate / "ai" / "models").is_dir():
            return candidate
    return _workspace_candidates()[-1]


def python_import_paths() -> str:
    """Colon-separated import roots for model venv re-exec (runfiles-aware)."""
    ordered: list[str] = []
    fallback: list[str] = []
    for candidate in _workspace_candidates():
        path = str(candidate)
        ai_root = Path(path) / "ai"
        if not ai_root.is_dir() or path in ordered or path in fallback:
            continue
        proto_pb2 = ai_root / "proto" / "ai_model_pb2.py"
        if proto_pb2.is_file():
            ordered.append(path)
        else:
            fallback.append(path)
    return os.pathsep.join(ordered + fallback)


def model_dir_for_type(model_type: int) -> str:
    """Map a ModelType enum value to its ai/models/<dir> name."""
    enum_name = ai_model_pb2.ModelType.Name(model_type)
    return enum_name.lower()


@lru_cache(maxsize=None)
def load_manifest(model_type: int) -> model_manifest_pb2.ModelManifest:
    """Load the model.textproto manifest for a ModelType."""
    model_dir = model_dir_for_type(model_type)
    manifest_path = repo_root() / "ai" / "models" / model_dir / "model.textproto"
    if not manifest_path.is_file():
        model_type_name = ai_model_pb2.ModelType.Name(model_type)
        raise ValueError(
            f"No manifest at {manifest_path} for model type "
            f"'{model_type_name}'. "
            f"Add ai/models/{model_dir}/model.textproto."
        )

    manifest = model_manifest_pb2.ModelManifest()
    text_format.Parse(manifest_path.read_text(), manifest)
    if manifest.model_type and manifest.model_type != model_type:
        raise ValueError(
            f"Manifest at {manifest_path} declares model_type "
            f"{ai_model_pb2.ModelType.Name(manifest.model_type)} but "
            f"{ai_model_pb2.ModelType.Name(model_type)} was requested."
        )
    return manifest


def import_entrypoint(entrypoint: str) -> Type[InferenceAdapter]:
    """Import an adapter class from 'module.path:ClassName'."""
    module_path, separator, class_name = entrypoint.partition(":")
    if not separator or not class_name:
        raise ValueError(
            f"Invalid adapter entrypoint '{entrypoint}'. " "Expected 'module:Class'."
        )
    module = import_module(module_path)
    adapter_class = getattr(module, class_name)
    if not isinstance(adapter_class, type) or not issubclass(
        adapter_class, InferenceAdapter
    ):
        raise TypeError(
            f"Entrypoint '{entrypoint}' did not resolve to an "
            "InferenceAdapter subclass."
        )
    return adapter_class
