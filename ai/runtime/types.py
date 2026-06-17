"""Data contracts for the inference runtime.

These types form the boundary between the ROS2 host (which owns all
pub/sub, message decoding, and publishing) and model adapters (which own
only model-specific preprocessing, inference, and postprocessing).

Schema-level contracts (the ``ChannelRole`` and ``TriggerMode`` enums and
the ``AdapterSpec`` message) are defined in
``ai/runtime/proto/runtime.proto`` and re-exported here so call sites have
a single import surface. The remaining types carry live runtime payloads
(numpy arrays, scalars) and stay as Python dataclasses since they are not
serialized and ``Observation.payload`` is not protobuf-representable.

Adapters never import rclpy or ROS message packages; they consume
``Observation`` objects and return ``ActionCommand`` objects.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from ai.runtime.proto import runtime_pb2

# Re-export proto-defined schema contracts.
ChannelRole = runtime_pb2.ChannelRole
TriggerMode = runtime_pb2.TriggerMode
AdapterSpec = runtime_pb2.AdapterSpec


def make_adapter_spec(
    trigger_mode: int = TriggerMode.EVENT,
    tick_hz: float = 0.0,
    min_subscriptions: int = 1,
    min_publishers: int = 1,
) -> "runtime_pb2.AdapterSpec":
    """Build an ``AdapterSpec`` with the runtime's defaults.

    Proto3 scalar defaults are zero, so this helper restores the intended
    defaults (>= 1 subscription/publisher) without each adapter repeating
    them.
    """
    return runtime_pb2.AdapterSpec(
        trigger_mode=trigger_mode,
        tick_hz=tick_hz,
        min_subscriptions=min_subscriptions,
        min_publishers=min_publishers,
    )


@dataclass(frozen=True)
class ChannelSpec:
    """Static description of one host subscription channel."""

    index: int
    topic: str
    ros2_data_type: int
    role: int  # ai.runtime.ChannelRole enum value


@dataclass(frozen=True)
class Observation:
    """A single decoded input handed to an adapter.

    Attributes:
        channel_index: Index into the node's subscription list.
        topic: Source ROS topic name.
        role: ``ChannelRole`` enum value (image/scalar/...).
        payload: Decoded payload -- a numpy array for IMAGE (HWC, rgb8),
            a float for SCALAR. Never a raw ROS message.
        timestamp_ns: Host receive time in nanoseconds.
    """

    channel_index: int
    topic: str
    role: int
    payload: Any
    timestamp_ns: int


@dataclass(frozen=True)
class ActionCommand:
    """A single action emitted by an adapter for one publisher.

    Attributes:
        publisher_index: Index into the node's publisher list.
        value: Scalar command value.
        normalized: If True, ``value`` is in [-1, 1] and the host
            denormalizes it using the actuator's operational limits
            before publishing.
    """

    publisher_index: int
    value: float
    normalized: bool = False
