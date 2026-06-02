import random
import threading
from typing import Any, Callable, List

from ai.proto.ai_model_pb2 import SingleModel

from ai.models.model_base import ModelBase


class RandomNoise(ModelBase):
    def __init__(self, config: SingleModel):
        super().__init__(config)

        # Initialize buffer to hold a list of values per subscription
        self._input_buffer: List[List[Any]] = [
            [] for _ in range(self._num_subscriptions)
        ]
        self._mutex = threading.Lock()

    def _validate_config(self) -> None:
        """Validate the model specific configuration."""
        if self._model_config.output_size == 0:
            raise ValueError("output_size must be greater than 0")

        if len(self._model_config.output_ranges) != self._model_config.output_size:
            raise ValueError(
                f"output_ranges length ({len(self._model_config.output_ranges)}) must "
                f"match output_size ({self._model_config.output_size})"
            )

        for index, output_range in enumerate(self._model_config.output_ranges):
            if output_range.low >= output_range.high:
                raise ValueError(
                    f"output_ranges[{index}]: low ({output_range.low}) must be less "
                    f"than high ({output_range.high})"
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
        processed_data = self.preprocess_input(subscription_index, data)

        with self._mutex:
            if subscription_index >= self._num_subscriptions:
                return

            self._input_buffer[subscription_index].append(processed_data)

            if len(self._input_buffer[subscription_index]) == 30:
                outputs = self.inference(self._input_buffer[subscription_index])
                final_outputs = self.postprocess_output(outputs)

                for publisher_index in range(self._num_publishers):
                    publish_callback(publisher_index, final_outputs[publisher_index])

                self._input_buffer[subscription_index] = []

    def preprocess_input(self, subscription_index: int, data: Any) -> Any:
        """Preprocess a single input. For RandomNoise, we just pass it through."""
        return data

    def postprocess_output(self, output_data: List[Any]) -> List[Any]:
        """Postprocess outputs. For RandomNoise, we just pass it through."""
        return output_data

    def inference(self, input_data: List[Any]) -> List[Any]:
        """Return random positions within each configured output range."""
        output_data = []
        for publisher_index in range(self._num_publishers):
            output_range = self._model_config.output_ranges[publisher_index]
            output_data.append(random.uniform(output_range.low, output_range.high))
        return output_data

    def forward(self, input_data: List[Any]) -> List[Any]:
        """Training forward pass. Not implemented for RandomNoise."""
        return []
