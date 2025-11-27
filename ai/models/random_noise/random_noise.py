import random
import threading
from typing import Any, Callable, List

from ai.proto.ai_model_pb2 import SingleModel

from ai.models.model_base import ModelBase


class RandomNoise(ModelBase):
    def __init__(self, config: SingleModel):
        super().__init__(config)

        # State for input tracking
        # Initialize buffer to hold a list of values per subscription
        self._input_buffer: List[List[Any]] = [
            [] for _ in range(self._num_subscriptions)
        ]
        self._mutex = threading.Lock()

    def _validate_config(self) -> None:
        """
        Validate the model specific configuration.
        """
        if self._model_config.noise_low >= self._model_config.noise_high:
            raise ValueError(
                f"Noise low ({self._model_config.noise_low}) must be less than noise high ({self._model_config.noise_high})"
            )

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
            self._input_buffer[subscription_index].append(processed_data)

            # Only run inference if the input buffer has size of 30. (This is ramdon behavior for now.)
            if len(self._input_buffer[subscription_index]) == 30:
                # 2. Inference
                outputs = self.inference(self._input_buffer[subscription_index])

                # 3. Postprocess
                final_outputs = self.postprocess_output(outputs)

                # 4. Publish
                publish_callback(final_outputs)

                # Reset the input buffer
                self._input_buffer[subscription_index] = []

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
            random.uniform(self._model_config.noise_low, self._model_config.noise_high)
            for _ in range(self._model_config.output_size)
        ]

        return output_data

    def forward(self, input_data: List[Any]) -> List[Any]:
        """
        Training forward pass. Not implemented for RandomNoise.
        """
        return []
