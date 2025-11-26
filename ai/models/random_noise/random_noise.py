import random
import threading
from typing import Any, List, Callable

from ai.models.model_base import ModelBase
from ai.models.random_noise.random_noise_config_pb2 import RandomNoiseConfig


class RandomNoise(ModelBase):
    def __init__(self, config: RandomNoiseConfig):
        self.config = config
        
        # State for input tracking
        self._input_buffer: List[Any] = []
        self._mutex = threading.Lock()
        self._num_subscriptions = 1 # TODO: Remove this hardcoded value and use with setup_inputs.

    # TODO: Rename this method to something else. This is not being called now.
    # TODO: Need numb_of_subscriptions * each input buffer.
    # TODO: num_subscriptions is not being passed in the constructor.
    def setup_inputs(self, num_subscriptions: int) -> None:
        """
        Initialize input buffers based on the number of subscriptions.
        """
        self._num_subscriptions = num_subscriptions
        self._input_buffer = [None] * num_subscriptions

    def handle_input(
        self,
        subscription_index: int,
        data: Any,
        publish_callback: Callable[[List[Any]], None],
    ) -> None:
        """
        Orchestrate the inference cycle:
        1. Preprocess
        2. Inference (synced)
        3. Postprocess
        4. Publish
        """
        # 1. Preprocess
        processed_data = self.preprocess_input(subscription_index, data)

        with self._mutex:
            if subscription_index >= self._num_subscriptions:
                return

            # Store the processed input data
            self._input_buffer.append(processed_data)

            input_snapshot = None
            
            if len(self._input_buffer) == 30:
                input_snapshot = list(self._input_buffer)
                self._input_buffer = []
            else:
                return

            # 2. Inference
            outputs = self.inference(input_snapshot)
            
            # 3. Postprocess
            final_outputs = self.postprocess_output(outputs)
            
            # 4. Publish
            publish_callback(final_outputs)

    def preprocess_input(self, subscription_index: int, data: Any) -> Any:
        """
        Preprocess a single input. For RandomNoise, we just pass it through.
        """
        return data

    def postprocess_output(self, output_data: List[Any]) -> List[Any]:
        """
        Postprocess outputs. For RandomNoise, we just pass it through.
        """
        return output_data

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

    def forward(self, input_data: List[Any]) -> List[Any]:
        """
        Training forward pass. Not implemented for RandomNoise.
        """
        return []
