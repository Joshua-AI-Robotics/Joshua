"""ROS2 inference host node.

``InferenceHost`` is a thin, model-agnostic rclpy node. It owns every
ROS concern -- selecting the SingleModel config for this node, creating
publishers/subscriptions with the configured QoS, decoding inbound
messages into canonical ``Observation`` objects, driving the adapter
(event- or tick-based), and publishing ``ActionCommand`` results.

All model-specific behavior lives in pluggable ``InferenceAdapter``
implementations resolved via the registry.
"""

from __future__ import annotations

import time
from typing import Any, List, NamedTuple, Optional

from rclpy.node import Node
from std_msgs.msg import Float32

from ai.models import registry
from ai.proto import ai_model_pb2
from ai.runtime.observation_codec import ObservationCodec
from ai.runtime.types import (
    ActionCommand,
    ChannelRole,
    ChannelSpec,
    Observation,
    TriggerMode,
)
from config.proto import config_pb2
from robot.action.proto import action_pb2
from ros2.proto import ros2_data_type_pb2
from ros2.ros2_type_resolver import resolve_message_class_from_enum
from ros2.utils.packet_parser import denormalize_position_value
from ros2.utils.qos_setting import create_qos_setting


class _Publisher(NamedTuple):
    instance: Any
    topic: str
    ros2_data_type: int


class InferenceHost(Node):
    """Model-agnostic ROS2 node hosting a single inference adapter."""

    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)

        self.node_id = node_id
        self.config = config
        self._codec = ObservationCodec()

        self.single_model_config = self._select_model_config()
        self._validate_node_config()

        self.adapter = registry.create_adapter(self.single_model_config, config)
        self.adapter.validate()
        self.adapter.initialize()
        self._spec = self.adapter.spec()
        self._validate_against_spec()

        self._pub_handles: List[_Publisher] = []
        self._setup_publishers()

        self._channels: List[ChannelSpec] = []
        self._setup_subscriptions()

        self._timer = None
        if self._spec.trigger_mode == TriggerMode.TICK:
            self._setup_tick_timer()

        node_cfg = self.single_model_config.node
        self.get_logger().info(
            f"Inference host '{node_name}' started "
            f"(adapter={type(self.adapter).__name__}, "
            f"mode={TriggerMode.Name(self._spec.trigger_mode)}): "
            f"{', '.join(p.topic for p in node_cfg.publishers)} publish topics, "
            f"{', '.join(s.topic for s in node_cfg.subscriptions)} subscribe topics."
        )

    def _select_model_config(self) -> ai_model_pb2.SingleModel:
        """Pick the SingleModel entry whose node id matches this node."""
        models = self.config.ai.models.single_models
        if len(models) == 0:
            raise ValueError("No SingleModel entries found in config.ai.models")

        selected = next((m for m in models if m.node.id == self.node_id), None)
        if not selected:
            raise ValueError(
                f"No SingleModel found with node_id={self.node_id} in config."
            )
        return selected

    def _validate_node_config(self) -> None:
        """Generic, model-agnostic node validation."""
        cfg = self.single_model_config
        if (
            cfg.model_type == ai_model_pb2.ModelType.MODEL_TYPE_INVALID
            or not cfg.model_type
        ):
            raise ValueError("Model type must be set in SingleModel config")

        if cfg.node.id == 0:
            raise ValueError("Node ID must be set in SingleModel config")

        if len(cfg.node.publishers) == 0:
            raise ValueError("At least one publisher is required in SingleModel config")

        if len(cfg.node.subscriptions) == 0:
            raise ValueError(
                "At least one subscription is required in SingleModel config"
            )

    def _validate_against_spec(self) -> None:
        """Check the node wiring satisfies the adapter's declared schema."""
        cfg = self.single_model_config.node
        if len(cfg.subscriptions) < self._spec.min_subscriptions:
            raise ValueError(
                f"Adapter requires >= {self._spec.min_subscriptions} subscriptions, "
                f"got {len(cfg.subscriptions)}."
            )
        if len(cfg.publishers) < self._spec.min_publishers:
            raise ValueError(
                f"Adapter requires >= {self._spec.min_publishers} publishers, "
                f"got {len(cfg.publishers)}."
            )
        if self._spec.trigger_mode == TriggerMode.TICK and self._spec.tick_hz <= 0:
            raise ValueError("TICK trigger mode requires spec.tick_hz > 0.")

    def _setup_publishers(self) -> None:
        qos_setting = create_qos_setting(self.single_model_config.node.qos_setting)
        for publisher in self.single_model_config.node.publishers:
            message_type = resolve_message_class_from_enum(publisher.ros2_data_type)
            instance = self.create_publisher(message_type, publisher.topic, qos_setting)
            self._pub_handles.append(
                _Publisher(
                    instance=instance,
                    topic=publisher.topic,
                    ros2_data_type=publisher.ros2_data_type,
                )
            )
            self.get_logger().info(
                f"Created publisher: {publisher.topic} "
                f"(type={message_type.__name__})"
            )

    def _setup_subscriptions(self) -> None:
        qos_setting = create_qos_setting(self.single_model_config.node.qos_setting)
        for index, subscription in enumerate(
            self.single_model_config.node.subscriptions
        ):
            message_type = resolve_message_class_from_enum(subscription.ros2_data_type)
            channel = ChannelSpec(
                index=index,
                topic=subscription.topic,
                ros2_data_type=subscription.ros2_data_type,
                role=self._codec.role_for(subscription.ros2_data_type),
            )
            self.create_subscription(
                message_type,
                subscription.topic,
                self._make_subscription_callback(channel),
                qos_setting,
            )
            self._channels.append(channel)
            self.get_logger().info(
                f"Created subscriber: {subscription.topic} "
                f"(type={message_type.__name__}, "
                f"role={ChannelRole.Name(channel.role)})"
            )

    def _setup_tick_timer(self) -> None:
        period_s = 1.0 / self._spec.tick_hz
        self._timer = self.create_timer(period_s, self._on_tick)
        self.get_logger().info(f"Tick timer running at {self._spec.tick_hz} Hz.")

    def _make_subscription_callback(self, channel: ChannelSpec):
        def _callback(msg: Any):
            try:
                payload = self._codec.decode(channel.ros2_data_type, msg)
                observation = Observation(
                    channel_index=channel.index,
                    topic=channel.topic,
                    role=channel.role,
                    payload=payload,
                    timestamp_ns=time.time_ns(),
                )
                commands = self.adapter.on_observation(observation)
            except Exception as e:  # noqa: BLE001 - isolate adapter failures
                self.get_logger().error(
                    f"Adapter failed handling '{channel.topic}': {e}"
                )
                return
            if commands:
                self._publish(commands)

        return _callback

    def _on_tick(self) -> None:
        try:
            commands = self.adapter.on_tick()
        except Exception as e:  # noqa: BLE001 - isolate adapter failures
            self.get_logger().error(f"Adapter tick failed: {e}")
            return
        if commands:
            self._publish(commands)

    def _publish(self, commands: List[ActionCommand]) -> None:
        for command in commands:
            self._publish_one(command)

    def _publish_one(self, command: ActionCommand) -> None:
        try:
            if not 0 <= command.publisher_index < len(self._pub_handles):
                raise ValueError(
                    f"publisher_index {command.publisher_index} out of range "
                    f"(have {len(self._pub_handles)} publishers)."
                )
            publisher = self._pub_handles[command.publisher_index]

            if publisher.ros2_data_type != ros2_data_type_pb2.Ros2DataType.FLOAT32:
                raise ValueError(
                    "Unsupported inference publisher ros2_data_type: "
                    f"{publisher.ros2_data_type}. Only FLOAT32 is supported."
                )

            value = float(command.value)
            if command.normalized:
                limits = self._lookup_operational_limits_by_topic(publisher.topic)
                if limits is None:
                    raise ValueError(
                        "normalized output requires actuator limits for topic "
                        f"'{publisher.topic}'"
                    )
                value = denormalize_position_value(value, limits[0], limits[1])

            ros_msg = Float32()
            ros_msg.data = value
            publisher.instance.publish(ros_msg)
        except Exception as e:  # noqa: BLE001 - never let publish kill the node
            self.get_logger().error(
                f"Error publishing to publisher {command.publisher_index}: {e}"
            )

    def _lookup_operational_limits_by_topic(
        self, topic: str
    ) -> Optional[tuple[float, float]]:
        for single_action in self.config.robot.actions.single_actions:
            if single_action.action_type != action_pb2.ActionType.ACTUATOR:
                continue
            for subscription in single_action.node.subscriptions:
                if subscription.topic == topic:
                    actuator = single_action.actuator
                    return (
                        float(actuator.operational_lower_limit),
                        float(actuator.operational_upper_limit),
                    )
        return None

    def shutdown(self) -> None:
        try:
            self.adapter.close()
        except Exception as e:  # noqa: BLE001
            self.get_logger().error(f"Adapter close failed: {e}")
