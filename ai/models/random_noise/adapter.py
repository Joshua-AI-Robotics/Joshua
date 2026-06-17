"""Random-noise inference adapter.

Emits random joint positions within configured ranges. Useful as a
wiring/smoke test for the inference pipeline without a real model.
"""

from __future__ import annotations

import random
import threading
from typing import List, Optional

from ai.proto.ai_model_pb2 import SingleModel

from ai.runtime.adapter import InferenceAdapter
from ai.runtime.types import ActionCommand, AdapterSpec, Observation, TriggerMode
from config.proto import config_pb2


class RandomNoiseAdapter(InferenceAdapter):
    """Publishes random actions after every N observations per channel."""

    BUFFER_TRIGGER = 30

    def __init__(self, single_model: SingleModel, config: config_pb2.Config):
        super().__init__(single_model, config)
        self._model_config = single_model.random_noise_config
        self._counts: List[int] = [0] * self._num_subscriptions
        self._mutex = threading.Lock()

    def spec(self) -> AdapterSpec:
        return AdapterSpec(
            trigger_mode=TriggerMode.EVENT,
            min_subscriptions=1,
            min_publishers=1,
        )

    def validate(self) -> None:
        if self._model_config.output_size == 0:
            raise ValueError("output_size must be greater than 0")

        if len(self._model_config.output_ranges) != self._model_config.output_size:
            raise ValueError(
                f"output_ranges length ({len(self._model_config.output_ranges)}) "
                f"must match output_size ({self._model_config.output_size})"
            )

        for index, output_range in enumerate(self._model_config.output_ranges):
            if output_range.low >= output_range.high:
                raise ValueError(
                    f"output_ranges[{index}]: low ({output_range.low}) must be "
                    f"less than high ({output_range.high})"
                )

        if self._model_config.output_size < self._num_publishers:
            raise ValueError(
                f"output_size ({self._model_config.output_size}) must cover all "
                f"{self._num_publishers} publishers."
            )

    def on_observation(self, observation: Observation) -> Optional[List[ActionCommand]]:
        with self._mutex:
            index = observation.channel_index
            if index >= self._num_subscriptions:
                return None

            self._counts[index] += 1
            if self._counts[index] < self.BUFFER_TRIGGER:
                return None
            self._counts[index] = 0

        return self._draw()

    def _draw(self) -> List[ActionCommand]:
        commands: List[ActionCommand] = []
        for publisher_index in range(self._num_publishers):
            output_range = self._model_config.output_ranges[publisher_index]
            commands.append(
                ActionCommand(
                    publisher_index=publisher_index,
                    value=random.uniform(output_range.low, output_range.high),
                    normalized=False,
                )
            )
        return commands
