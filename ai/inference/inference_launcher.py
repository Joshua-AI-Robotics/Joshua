"""Inference launcher: resolve model from config and re-exec into its environment."""

from __future__ import annotations

import os
import sys
from typing import List, Optional

from google.protobuf import text_format

from ai.inference.environment_manager import ensure_model_env
from ai.inference.manifest import load_manifest, python_import_paths
from ai.inference.proto import model_manifest_pb2
from config.proto import config_pb2


def _parse_cli(argv: List[str]) -> tuple[str, int, str]:
    if len(argv) < 4:
        raise ValueError("Usage: <binary> <node_name> <node_id> <config_path>")
    node_name = argv[1]
    try:
        node_id = int(argv[2])
    except ValueError as exc:
        raise ValueError("node_id must be an integer") from exc
    config_path = argv[3]
    return node_name, node_id, config_path


def _load_config(config_path: str) -> config_pb2.Config:
    cfg = config_pb2.Config()
    with open(config_path, encoding="utf-8") as handle:
        text_format.Parse(handle.read(), cfg)
    return cfg


def _model_type_for_node(config: config_pb2.Config, node_id: int) -> int:
    models = config.ai.models.single_models
    selected = next((model for model in models if model.node.id == node_id), None)
    if selected is None:
        raise ValueError(f"No SingleModel found with node_id={node_id} in config.")
    if not selected.model_type:
        raise ValueError(f"Model type must be set for node_id={node_id}.")
    return selected.model_type


def _already_in_model_env(model_name: str) -> bool:
    return os.environ.get("JOSHUA_MODEL_ENV") == model_name


def _model_pythonpath() -> str:
    """Import roots for model venv; prefer Bazel runfiles with generated protos."""
    return python_import_paths()


def _reexec_into_model_env(
    manifest: model_manifest_pb2.ModelManifest, argv: List[str]
) -> None:
    venv_py = ensure_model_env(manifest)
    env = os.environ.copy()
    env["JOSHUA_MODEL_ENV"] = manifest.name
    env["PYTHONNOUSERSITE"] = "1"
    env["PYTHONPATH"] = _model_pythonpath()
    if runfiles := os.environ.get("RUNFILES_DIR"):
        env["RUNFILES_DIR"] = runfiles
    os.execve(
        venv_py,
        [venv_py, "-s", "-m", "ai.inference.host_main", *argv[1:]],
        env,
    )


def main(argv: Optional[List[str]] = None) -> int:
    argv = list(sys.argv if argv is None else argv)
    _, node_id, config_path = _parse_cli(argv)
    config = _load_config(config_path)
    model_type = _model_type_for_node(config, node_id)
    manifest = load_manifest(model_type)

    if not _already_in_model_env(manifest.name):
        _reexec_into_model_env(manifest, argv)

    from ai.inference.host_main import main as host_main

    return host_main(argv)


if __name__ == "__main__":
    sys.exit(main())
