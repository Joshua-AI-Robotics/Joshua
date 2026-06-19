"""Plugin registry backed by per-model manifests."""

from __future__ import annotations

from typing import Type

from ai.proto import ai_model_pb2
from ai.inference.adapter import InferenceAdapter
from ai.inference.manifest import import_entrypoint, load_manifest
from config.proto import config_pb2


def get_adapter_class(model_type: int) -> Type[InferenceAdapter]:
    """Return the adapter class registered for a ModelType."""
    manifest = load_manifest(model_type)
    return import_entrypoint(manifest.entrypoint)


def create_adapter(
    single_model: ai_model_pb2.SingleModel, config: config_pb2.Config
) -> InferenceAdapter:
    """Instantiate the adapter for a SingleModel config entry."""
    adapter_class = get_adapter_class(single_model.model_type)
    return adapter_class.from_config(single_model, config)
