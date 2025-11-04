import threading
from typing import List, Any, Callable
from abc import ABC, abstractmethod

import rclpy
from rclpy.node import Node
from ros2.ros2_type_resolver import resolve_message_class_from_enum

# Protobuf generated modules
from config.proto import config_pb2
 

class InferenceBase(Node, ABC):
    """
    Base class for inference nodes in the Joshua robotics system.
    
    This class handles:
    - ROS2 publisher/subscriber setup
    - State tracking from multiple sensor topics
    - Synchronized action publishing
    
    Subclasses should implement the `infer()` method to provide
    specific inference logic (mock, smolVLA, pi0, etc.)
    """
    
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)
        
        self.node_id = node_id
        self.config = config
        self._single_model = self._select_single_model()
        
        # Validate configuration
        if not self._validate_config():
            self.get_logger().error("Configuration validation failed.")
            return
        
        # Setup publishers
        self.publishers_list: List[rclpy.publisher.Publisher] = []
        self.publishers_msg_types: List[Any] = []
        self._setup_publishers()

        # Setup subscriptions
        self.subscriptions_list: List[rclpy.subscription.Subscription] = []
        self._setup_subscriptions()
        
        # Initialize input tracking
        # Store raw input data for inference
        num_inputs = len(self._single_model.subscriptions)
        self.latest_input_data: List[Any] = [None for _ in range(num_inputs)]
        self.received_flags: List[bool] = [False for _ in range(num_inputs)]
        
        # Thread safety for input updates
        self._mutex = threading.Lock()
        
        # Call subclass-specific initialization
        self._validate_config()
        
        self.get_logger().info(
            f"Inference node '{node_name}' started: "
            f"{len(self._single_model.pubishers)} publish topics, "
            f"{len(self._single_model.subscriptions)} subscribe topics."
        )

    def _select_single_model(self):
        """
        Select the SingleModel config for this node by matching node_id.
        If multiple models exist and none match, this returns the first model.
        """
        models = self.config.ai.models.single_model
        if len(models) == 0:
            raise ValueError("No SingleModel entries found in config.ai.models")
        for m in models:
            if m.node_id == self.node_id:
                return m
        if len(models) == 1:
            return models[0]
        self.get_logger().warn(
            f"No SingleModel with node_id={self.node_id}; defaulting to first model")
        return models[0]
    
    def _validate_config(self) -> None:
        """
        Validate configuration. Override in subclass for specific checks.
        """
        return True
    
    def _setup_publishers(self) -> None:
        """
        Set up ROS2 publishers for inference outputs.
        Derived classes can customize topics, QoS, etc.
        """
        for publisher_info in self._single_model.pubishers:
            message_type = resolve_message_class_from_enum(publisher_info.ros2_data_type)
            topic = publisher_info.topic
            self.publishers_list.append(self.create_publisher(message_type, topic, 10))
            self.publishers_msg_types.append(message_type)
            self.get_logger().info(
                f"Created publisher: {topic} (type={message_type.__name__})")
    
    def _setup_subscriptions(self) -> None:
        """
        Set up ROS2 subscriptions for inference inputs.
        Derived classes can customize topics, QoS, etc.
        """
        for input_index, subscription_info in enumerate(self._single_model.subscriptions):
            message_type = resolve_message_class_from_enum(subscription_info.ros2_data_type)
            topic = subscription_info.topic
            subscription = self.create_subscription(message_type, topic, self._make_sensor_callback(input_index), 10)
            self.subscriptions_list.append(subscription)
            self.get_logger().info(f"Created subscriber: {topic} (type={message_type.__name__})")

    def _make_sensor_callback(self, input_index: int) -> Callable[[Any], None]:
        """
        Make a callback function for the sensor data.
        """
        def callback(msg: Any) -> None:
            with self._mutex:
                if input_index >= len(self.latest_input_data):
                    self.get_logger().error(f"Input index {input_index} out of range")
                    return

                # Store the sensor data
                self.latest_input_data[input_index] = msg
                self.received_flags[input_index] = True

                # if we have a fresh message from all topics, run the inference and publish the outputs
                if all(self.received_flags):
                    self._run_inference_and_publish()
                    # reset the received flags
                    self.received_flags = [False for _ in range(len(self.received_flags))]
        return callback
    
    def _run_inference_and_publish(self) -> None:
        """
        Run inference pipeline on collected input data and publish outputs.

        Pipeline:
        1. Process raw sensor data (preprocessing, normalization, etc.)
        2. Run model inference
        3. Process model outputs into ROS messages
        4. Publish to action topics
        """
        
        try:

            processed_inputs = self._process_sensor_data(self.latest_input_data)
            model_outputs = self._run_model_inference(processed_inputs)
            ros_messages =self._process_model_outputs_to_ros_messages(model_outputs)
            self._publish_outputs(ros_messages)

        except Exception as e:
            self.get_logger().error(f"Error in inference pipeline: {e}")
            return

    def _publish_outputs(self, ros_messages: List[Any]) -> None:
        """
        Publish ROS messages to the appropriate topics.
        """
        if len(ros_messages) != len(self.publishers_list):
            raise ValueError(f"Number of ROS messages ({len(ros_messages)}) does not match number of publishers ({len(self.publishers_list)})")
        for publisher, message in zip(self.publishers_list, ros_messages):
            publisher.publish(message)

    def _process_model_outputs_to_ros_messages(self, model_outputs: Any) -> List[Any]:
        """
        Process model outputs into ROS messages.
        """
        
        ros_messages = []
        for i , action_value in enumerate(model_outputs):
            message_type = self.publishers_msg_types[i]
            message = message_type()
            message.data = action_value
            ros_messages.append(message)
        return ros_messages

    @abstractmethod
    def _process_sensor_data(self, sensor_data: List[Any]) -> Any:
        """
        Process raw sensor data into a format suitable for model inference.

        Args:
            sensor_data: List of Ros messages of sensor data (e.g. Image, PointCloud, etc.)
        
        Returns:
            Processed sensor data suitable for model inference
        """
        pass

    @abstractmethod
    def _run_model_inference(self, processed_inputs: Any) -> Any:
        """
        Run model inference on the processed inputs.

        Args:
            processed_inputs: Processed input data suitable for model inference
        
        Returns:
            Model outputs
        """
        pass

