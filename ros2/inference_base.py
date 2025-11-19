import threading
from typing import List, Any
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
    
    Subclasses should implement the `_run_model_inference()` method to provide
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
        self._initialize_inference()
        
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
    
    def _validate_config(self) -> bool:
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
            subscription = self.create_subscription(message_type, topic, self._make_subscription_callback(input_index), 10)
            self.subscriptions_list.append(subscription)
            self.get_logger().info(f"Created subscriber: {topic} (type={message_type.__name__})")
    
    # TODO: We need to make multiple callback option.
    # 1) All inputs are ready, run inference and publish (current).
    # 2) Timing-based inference (e.g., every 100ms, run inference and publish).
    # 3) Other options? Consider padding to 0 for missing inputs.
    def _make_subscription_callback(self, input_index: int):
        """
        Create a callback function for a specific input topic.
        """
        def _callback(msg: Any):
            with self._mutex:
                if input_index >= len(self.latest_input_data):
                    self.get_logger().warn(f"Received input index out of range: {input_index}")
                    return
                
                # Store the raw input data
                self.latest_input_data[input_index] = msg
                self.received_flags[input_index] = True
                
                # If we have fresh data from all inputs, take a snapshot and reset flags
                should_run_inference = False
                input_snapshot = None
                if all(self.received_flags):
                    input_snapshot = list(self.latest_input_data)
                    # Reset flags for next round
                    self.received_flags = [False for _ in self.received_flags]
                    should_run_inference = True
            
            # Run inference and publish outside the lock to avoid blocking other callbacks
            if should_run_inference and input_snapshot is not None:
                outputs = self._run_model_inference(input_snapshot)
                self._publish_output(outputs)
        
        return _callback
    
    def _publish_output(self, output_values: List[Any]) -> None:
        """Publish the messages from publishers."""
        if len(output_values) != len(self.publishers_list):
            self.get_logger().error(
                    f"Inference returned {len(output_values)} outputs, "
                    f"but expected {len(self.publishers_list)}"
            )
            return

        try:
            for i, (publisher, value) in enumerate(zip(self.publishers_list, output_values)):
                msg_cls = self.publishers_msg_types[i]
                msg = msg_cls()
                if hasattr(msg, "data"):
                    msg.data = value
                else:
                    if isinstance(value, msg_cls):
                        msg = value
                    else:
                        raise ValueError(
                            f"Output at index {i} must be instance of {msg_cls.__name__} or a value for .data")
                publisher.publish(msg)
            
        except Exception as e:
            self.get_logger().error(f"Inference error: {str(e)}")

    
    @abstractmethod
    def _initialize_inference(self) -> None:
        """
        Initialize inference-specific components (e.g., load model, set parameters).
        Called during __init__ after ROS2 setup is complete.
        """
        pass
    
    @abstractmethod
    def _run_model_inference(self, input_data: List[Any]) -> List[Any]:
        """
        Run inference on input data and return output data.
        
        Args:
            input_data: List of input messages (e.g., Image messages from sensors)
        
        Returns:
            List of output messages (one per publisher)
        """
        pass

