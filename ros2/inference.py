import sys
from typing import Any, List, NamedTuple, Union

import rclpy
from rclpy.node import Node

from ai.models import model_registry
from ai.proto import ai_model_pb2
from config.proto import config_pb2
from ros2 import node_runner as node_runner_py
from ros2.ros2_type_resolver import resolve_message_class_from_enum
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

    def _publish_output(self, publisher_index: int, output_value: Any) -> None:
        """Publish the messages from publishers."""
        try:
            publisher_instance = self.publisher_list[publisher_index]
            publisher = publisher_instance.instance
            msg_type = publisher_instance.msg_type

            if not isinstance(output_value, msg_type):
                try:
                    msg = msg_type()
                    if hasattr(msg, "data"):
                        msg.data = output_value
                        output_value = msg
                except Exception:
                    pass

            publisher.publish(output_value)
            self.get_logger().info(
                f"Published output to publisher {publisher_index}: {output_value}"
            )

        except Exception as e:
            self.get_logger().error(
                f"Error publishing output to publisher {publisher_index}: {str(e)}"
            )


def main(argv=None):
    return node_runner_py.run_node(Inference, logger_name="inference", argv=argv)


if __name__ == "__main__":
    sys.exit(main())
