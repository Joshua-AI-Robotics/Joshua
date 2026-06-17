"""Plugin registry: the single wiring point between models and the engine.

Maps each ``ModelType`` to its ``InferenceAdapter`` implementation. This
is the one module that knows about every model, so the engine
(``ai/runtime``) stays model-agnostic and the per-model packages stay
self-contained.

Adapter modules are imported lazily inside the loader functions so that
heavy backends (e.g. SmolVLA pulling torch/lerobot) are only imported
when actually selected by the config.
"""

from __future__ import annotations

from typing import Callable, Dict, Type

from ai.proto import ai_model_pb2
from ai.runtime.adapter import InferenceAdapter
from config.proto import config_pb2


def _load_random_noise() -> Type[InferenceAdapter]:
    from ai.models.random_noise.adapter import RandomNoiseAdapter

    return RandomNoiseAdapter


def _load_smolvla() -> Type[InferenceAdapter]:
    from ai.models.smolvla.adapter import SmolVlaAdapter

    return SmolVlaAdapter


# Map ModelType enum -> lazy loader returning the adapter class.
ADAPTER_LOADERS: Dict[int, Callable[[], Type[InferenceAdapter]]] = {
    ai_model_pb2.ModelType.RANDOM_NOISE: _load_random_noise,
    ai_model_pb2.ModelType.SMOLVLA: _load_smolvla,
}


def get_adapter_class(model_type: int) -> Type[InferenceAdapter]:
    """Return the adapter class registered for a ModelType."""
    loader = ADAPTER_LOADERS.get(model_type)
    if loader is None:
        model_type_name = ai_model_pb2.ModelType.Name(model_type)
        raise ValueError(
            f"Model type '{model_type_name}' (enum value: {model_type}) is not "
            "registered. Add a loader in ai/models/registry.py."
        )
    return loader()


def create_adapter(
    single_model: ai_model_pb2.SingleModel, config: config_pb2.Config
) -> InferenceAdapter:
    """Instantiate the adapter for a SingleModel config entry."""
    adapter_class = get_adapter_class(single_model.model_type)
    return adapter_class.from_config(single_model, config)
