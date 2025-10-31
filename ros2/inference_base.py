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
        
        self.action_publishers: List[rclpy.publisher.Publisher] = []
        self._publisher_msg_types: List[Any] = []
        self._setup_publishers()
        
        # Initialize state tracking
        # Store raw input data for inference
        num_subs = len(self._single_model.subscriptions)
        self.latest_sensor_data: List[Any] = [None for _ in range(num_subs)]
        self.received_flags: List[bool] = [False for _ in range(num_subs)]
        
        # Thread safety for state updates
        self._mutex = threading.Lock()
        
        # Initialize subscriptions to state topics
        self._setup_subscriptions()
        
        # Call subclass-specific initialization
        self._initialize_inference()
        
        self.get_logger().info(
            f"Inference node '{node_name}' started: "
            f"{len(self._single_model.pubishers)} action outputs, "
            f"{len(self._single_model.subscriptions)} sensor inputs."
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
        Set up ROS2 publishers for action outputs.
        Derived classes can customize topics, QoS, etc.
        """
        for publisher_info in self._single_model.pubishers:
            message_type = resolve_message_class_from_enum(publisher_info.ros2_data_type)
            topic = publisher_info.topic
            self.action_publishers.append(self.create_publisher(message_type, topic, 10))
            self._publisher_msg_types.append(message_type)
            self.get_logger().info(
                f"Created publisher for action topic: {topic} (type={message_type.__name__})")
    
    def _setup_subscriptions(self) -> None:
        """
        Set up ROS2 subscriptions for inference input.
        Derived classes can customize topics, QoS, etc.
        """
        self.state_subscriptions = []
        for i, subscription_info in enumerate(self._single_model.subscriptions):
            message_type = resolve_message_class_from_enum(subscription_info.ros2_data_type)
            topic = subscription_info.topic
            self.state_subscriptions.append(
                self.create_subscription(message_type, topic, self._make_sensor_callback(i), 10)
            )
            self.get_logger().info(f"Subscribed to sensor topic: {topic} (type={message_type.__name__})")
    
    def _make_sensor_callback(self, sensor_index: int):
        """
        Create a callback function for a specific sensor topic.
        """
        def _callback(msg: Any):
            with self._mutex:
                if sensor_index >= len(self.latest_sensor_data):
                    self.get_logger().warn(f"Received sensor index out of range: {sensor_index}")
                    return
                
                # Store the raw sensor data
                self.latest_sensor_data[sensor_index] = msg
                self.received_flags[sensor_index] = True
                
                # If we have fresh data from all sensors, run inference and publish
                if all(self.received_flags):
                    self._run_inference_and_publish_locked()
                    # Reset flags for next round
                    self.received_flags = [False for _ in self.received_flags]
        
        return _callback
    
    
    def _run_inference_and_publish_locked(self) -> None:
        """
        Run inference on collected sensor data and publish actions.
        This method assumes the mutex is already held.
        """
        if not all(data is not None for data in self.latest_sensor_data):
            self.get_logger().warn("Not all sensor data available for inference")
            return
        
        try:
            # Call subclass-specific inference
            actions = self.infer(self.latest_sensor_data)
            
            # Validate action outputs
            if len(actions) != len(self.action_publishers):
                self.get_logger().error(
                    f"Inference returned {len(actions)} actions, "
                    f"but expected {len(self.action_publishers)}"
                )
                return
            
            # Publish actions
            for i, (publisher, action_value) in enumerate(zip(self.action_publishers, actions)):
                msg_cls = self._publisher_msg_types[i]
                msg = msg_cls()
                if hasattr(msg, "data"):
                    msg.data = action_value
                else:
                    if isinstance(action_value, msg_cls):
                        msg = action_value
                    else:
                        raise ValueError(
                            f"Action at index {i} must be instance of {msg_cls.__name__} or a value for .data")
                publisher.publish(msg)
            
        except Exception as e:
            self.get_logger().error(f"Inference failed: {str(e)}")
    
    @abstractmethod
    def _initialize_inference(self) -> None:
        """
        Initialize inference-specific components (e.g., load model, set parameters).
        Called during __init__ after ROS2 setup is complete.
        """
        pass
    
    @abstractmethod
    def infer(self, input_data: List[Any]) -> List[Any]:
        """
        Run inference on input data and return output data.
        
        Args:
            input_data: List of input messages (e.g., Image messages from cameras)
        
        Returns:
            List of output messages (one per publisher)
        """
        pass

