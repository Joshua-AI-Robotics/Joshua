from typing import Dict, Type

from ai.models.model_base import ModelBase

# Import your models here
from ai.models.random_noise.random_noise import RandomNoise
from ai.models.smolvla.smolvla import SmolVla
from ai.proto import ai_model_pb2

# Registry mapping ModelType enum to Model Class
MODEL_REGISTRY: Dict[int, Type[ModelBase]] = {
    ai_model_pb2.ModelType.RANDOM_NOISE: RandomNoise,
    ai_model_pb2.ModelType.SMOLVLA: SmolVla,
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
    return MODEL_REGISTRY[model_type]
