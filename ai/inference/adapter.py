"""Inference adapter plugin contract.

An ``InferenceAdapter`` encapsulates everything model-specific:
checkpoint loading, preprocessing, inference, postprocessing, and the
buffering/synchronization policy. It is intentionally ROS-free -- the
host decodes ROS messages into ``Observation`` objects and publishes the
``ActionCommand`` objects returned here.

Adding a new model means writing one adapter and registering it; no
changes to the ROS host are required.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import List, Optional

from ai.proto.ai_model_pb2 import SingleModel

from ai.inference.types import ActionCommand, AdapterSpec, Observation
from config.proto import config_pb2


class InferenceAdapter(ABC):
    """Base class for all model inference plugins."""

    def __init__(self, single_model: SingleModel, config: config_pb2.Config):
        self._single_model = single_model
        self._config = config
        self._num_subscriptions = len(single_model.node.subscriptions)
        self._num_publishers = len(single_model.node.publishers)

    @classmethod
    def from_config(
        cls, single_model: SingleModel, config: config_pb2.Config
    ) -> "InferenceAdapter":
        """Factory used by the registry. Override for custom construction."""
        return cls(single_model, config)

    @abstractmethod
    def spec(self) -> AdapterSpec:
        """Declare the static schema/timing contract for host validation."""

    def validate(self) -> None:
        """Validate model-specific config. Raise ValueError on error."""

    def initialize(self) -> None:
        """Load weights / build processors. Called once before serving."""

    @abstractmethod
    def on_observation(self, observation: Observation) -> Optional[List[ActionCommand]]:
        """Handle one decoded observation.

        Return a list of ``ActionCommand`` to publish immediately, or
        ``None`` when not ready (e.g. still buffering or awaiting other
        camera frames).
        """

    def on_tick(self) -> Optional[List[ActionCommand]]:
        """Fixed-rate hook, only called when spec.trigger_mode is TICK."""
        return None

    def close(self) -> None:
        """Release resources on shutdown."""
