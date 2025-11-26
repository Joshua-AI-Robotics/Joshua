import random
from typing import Any, List

from ai.models.model_base import ModelBase
from ai.models.random_noise_config_py_proto import RandomNoiseConfig


class RandomNoise(ModelBase):
    def __init__(self, config: RandomNoiseConfig):
        super().__init__()
        self.config = config
        self.noise_low = config.noise_low
        self.noise_high = config.noise_high

    def inference(self, input_data: List[Any]) -> List[Any]:
        """
        Run inference on the input data and return the output data.
        """

        # Add random noise to the input data
        output_data = [
            input_data[i]
            + input_data[i] * random.uniform(self.noise_low, self.noise_high)
            for i in range(len(input_data))
        ]

        return output_data
