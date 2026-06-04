import sys
import time
from typing import Any, List, NamedTuple, Optional, Union

import rclpy
from rclpy.node import Node
from std_msgs.msg import Float32

from ai.models import model_registry
from ai.proto import ai_model_pb2
from config.proto import config_pb2
from robot.action.proto import action_packet_pb2
from ros2 import node_runner as node_runner_py
from ros2.proto import ros2_data_type_pb2
from ros2.ros2_type_resolver import resolve_message_class_from_enum
from ros2.utils.packet_parser import (
    denormalize_position_value,
    extract_scalar_from_action,
)
from ros2.utils.qos_setting import create_qos_setting


class PubSubInstance(NamedTuple):
    """
    Holds the ROS2 publisher or subscription instance and its associated message type.

    Attributes:
        instance: The rclpy Publisher or Subscription object.
        msg_type: The ROS2 message class (e.g., std_msgs.msg.String).
    """

    instance: Union[rclpy.publisher.Publisher, rclpy.subscription.Subscription]
    msg_type: Any


class Inference(Node):
    """
    AI inference node for the Joshua robotics system.

    This class handles the following:
    - ROS2 publisher/subscriber setup
    - Delegate input handling to the model
    - Delegate output publishing to the model
    """

    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)

        self.node_id = node_id
        self.config = config
        self.single_model_config, self.inference_model = self._initialize_model()

        self._validate_config()

        self.publisher_list: List[PubSubInstance] = []
        self._setup_publishers()

        self.subscription_list: List[PubSubInstance] = []
        self._setup_subscriptions()

        self.get_logger().info(
            f"Inference node '{node_name}' started: "
            f"{', '.join([publisher.topic for publisher in self.single_model_config.node.publishers])} publish topics, "
            f"{', '.join([subscription.topic for subscription in self.single_model_config.node.subscriptions])} subscribe topics."
        )

    def _initialize_model(self):
        """
        Select the SingleModel config for this node by matching node_id and matching model config.
        Also initializes the model based on the selected SingleModel config.
        """
        models = self.config.ai.models.single_models
        if len(models) == 0:
            raise ValueError("No SingleModel entries found in config.ai.models")

        selected_model_config = next(
            (m for m in models if m.node.id == self.node_id), None
        )
        if not selected_model_config:
            raise ValueError(
                f"No SingleModel found with node_id={self.node_id} in config."
            )

        # Retrieve the model class from the registry
        model_type = selected_model_config.model_type
        try:
            model_class = model_registry.get_model_class(model_type)
        except ValueError as e:
            raise ValueError(f"Model class lookup failed: {e}")

        # Get the specific config from the oneof field
        config_field = selected_model_config.WhichOneof("model_config")
        if not config_field:
            model_type_name = ai_model_pb2.ModelType.Name(model_type)
            raise ValueError(
                f"No model config field set in SingleModel for model type '{model_type_name}'"
            )

        try:
            model_instance = model_class(selected_model_config)
            self.get_logger().info(
                f"Initialized model '{model_class.__name__}' with config field '{config_field}'"
            )
        except Exception as e:
            raise ValueError(
                f"Failed to instantiate model '{model_class.__name__}': {e}"
            )

        return selected_model_config, model_instance

    def _validate_config(self) -> None:
        """
        Validate configuration. Model spcific validation should be done in the model class.
        """
        if (
            self.single_model_config.model_type
            == ai_model_pb2.ModelType.MODEL_TYPE_INVALID
            or not self.single_model_config.model_type
        ):
            raise ValueError("Model type must be set in SingleModel config")

        if self.single_model_config.node.id == 0:
            raise ValueError("Node ID must be set in SingleModel config")

        if len(self.single_model_config.node.publishers) == 0:
            raise ValueError("At least one publisher is required in SingleModel config")

        if len(self.single_model_config.node.subscriptions) == 0:
            raise ValueError(
                "At least one subscription is required in SingleModel config"
            )

    def _setup_publishers(self) -> None:
        """
        Set up ROS2 publishers for inference outputs.
        """
        qos_setting = create_qos_setting(self.single_model_config.node.qos_setting)
        for publisher in self.single_model_config.node.publishers:
            message_type = resolve_message_class_from_enum(publisher.ros2_data_type)
            topic = publisher.topic
            self.publisher_list.append(
                PubSubInstance(
                    instance=self.create_publisher(message_type, topic, qos_setting),
                    msg_type=message_type,
                )
            )
            self.get_logger().info(
                f"Created publisher: {topic} (type={message_type.__name__})"
            )

    def _setup_subscriptions(self) -> None:
        """
        Set up ROS2 subscriptions for inference inputs.
        Derived classes can customize topics, QoS, etc.
        """
        qos_setting = create_qos_setting(self.single_model_config.node.qos_setting)
        for subscription_index, subscription in enumerate(
            self.single_model_config.node.subscriptions
        ):
            message_type = resolve_message_class_from_enum(subscription.ros2_data_type)
            topic = subscription.topic
            subscription = self.create_subscription(
                message_type,
                topic,
                self._make_subscription_callback(subscription_index),
                qos_setting,
            )
            self.subscription_list.append(
                PubSubInstance(instance=subscription, msg_type=message_type)
            )
            self.get_logger().info(
                f"Created subscriber: {topic} (type={message_type.__name__})"
            )

    def _make_subscription_callback(self, subscription_index: int):
        """
        Create a callback function for a specific input topic.
        Delegates processing to the model.
        """

        def _callback(msg: Any):
            # Delegate to the model to decide how to process input and when to publish
            self.inference_model.handle_input(
                subscription_index, msg, self._publish_output
            )

        return _callback

    def _to_action_packet(
        self, output_value: Any
    ) -> Optional[action_packet_pb2.ActionPacket]:
        """Normalize model output to ActionPacket (default inference payload)."""
        if isinstance(output_value, action_packet_pb2.ActionPacket):
            return output_value

        if isinstance(output_value, (int, float)):
            packet = action_packet_pb2.ActionPacket()
            packet.position = float(output_value)
            packet.timestamp_ns = time.time_ns()
            return packet

        return None

    def _lookup_operational_limits_by_topic(
        self, topic: str
    ) -> tuple[float, float] | None:
        for single_action in self.config.robot.actions.single_actions:
            if single_action.action_type != single_action.ACTUATOR:
                continue
            for subscription in single_action.node.subscriptions:
                if subscription.topic == topic:
                    actuator = single_action.actuator
                    return (
                        float(actuator.operational_lower_limit),
                        float(actuator.operational_upper_limit),
                    )
        return None

    def _publish_output(self, publisher_index: int, output_value: Any) -> None:
        """Publish model output as ActionPacket on the configured ROS topic."""
        try:
            publisher_instance = self.publisher_list[publisher_index]
            publisher = publisher_instance.instance
            pub_cfg = self.single_model_config.node.publishers[publisher_index]

            packet = self._to_action_packet(output_value)
            if packet is None:
                self.get_logger().error(
                    f"Could not convert output for publisher {publisher_index} to "
                    f"ActionPacket (type={type(output_value).__name__})."
                )
                return

            if packet.timestamp_ns == 0:
                packet.timestamp_ns = time.time_ns()

            if pub_cfg.ros2_data_type != ros2_data_type_pb2.FLOAT32:
                raise ValueError(
                    f"Unsupported inference publisher ros2_data_type: {pub_cfg.ros2_data_type}. "
                    "Only FLOAT32 is supported."
                )

            ros_msg = Float32()
            which = packet.WhichOneof("action_type")
            value = extract_scalar_from_action(packet)
            if value is None:
                raise ValueError(
                    f"ActionPacket has no scalar action_type for FLOAT32 publish "
                    f"(action_type={which!r})."
                )
            if which == "position" and packet.normalized:
                limits = self._lookup_operational_limits_by_topic(pub_cfg.topic)
                if limits is None:
                    raise ValueError(
                        f"normalized position output requires actuator limits for topic '{pub_cfg.topic}'"
                    )
                value = denormalize_position_value(value, limits[0], limits[1])
            ros_msg.data = value
            publisher.publish(ros_msg)

        except Exception as e:
            self.get_logger().error(
                f"Error publishing output to publisher {publisher_index}: {str(e)}"
            )


def main(argv=None):
    return node_runner_py.run_node(Inference, logger_name="inference", argv=argv)


if __name__ == "__main__":
    sys.exit(main())
