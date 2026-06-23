from typing import Callable, Dict, Type

from ai.models.model_base import ModelBase

from ai.models.random_noise.random_noise import RandomNoise
from ai.proto import ai_model_pb2


def _load_smolvla() -> Type[ModelBase]:
    try:
        from ai.models.smolvla.smolvla import SmolVla
    except ImportError as exc:
        raise ImportError(
            "SmolVLA requires the optional LeRobot dependency stack. "
            "Install/enable those dependencies before using ModelType.SMOLVLA."
        ) from exc
    return SmolVla


MODEL_REGISTRY: Dict[int, Callable[[], Type[ModelBase]]] = {
    ai_model_pb2.ModelType.RANDOM_NOISE: lambda: RandomNoise,
    ai_model_pb2.ModelType.SMOLVLA: _load_smolvla,
}


def get_model_class(model_type: int) -> Type[ModelBase]:
    """
    Retrieve the model class for a given model type.
    """
    if model_type not in MODEL_REGISTRY:
        model_type_name = ai_model_pb2.ModelType.Name(model_type)
        raise ValueError(
            f"Model type '{model_type_name}' (enum value: {model_type}) is not registered in MODEL_REGISTRY."
        )
    return MODEL_REGISTRY[model_type]()
