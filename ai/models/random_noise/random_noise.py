import random
from typing import Any, List

from ai.models.model_base import ModelBase
from ai.models.random_noise.random_noise_config_pb2 import RandomNoiseConfig


class RandomNoise(ModelBase):
    def __init__(self, config: RandomNoiseConfig):
        super().__init__()
        self.config = config

    def inference(self, input_data: List[Any]) -> List[Any]:
        """
        Run inference on the input data and return the output data.
        """

        # Return random action values.
        output_data = [
            random.uniform(self.config.noise_low, self.config.noise_high)
            for _ in range(self.config.output_size)
        ]

        return output_data
