import threading
from typing import List, Any
from abc import ABC, abstractmethod

import rclpy
from rclpy.node import Node
from std_msgs.msg import Float32
from sensor_msgs.msg import Image

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
        
        # Validate configuration
        if not self._validate_config():
            self.get_logger().error("Configuration validation failed.")
            return
        
        # Initialize publishers for action outputs
        self.action_publishers: List[rclpy.publisher.Publisher] = []
        for topic in config.ai.publish_topics:
            self.action_publishers.append(self.create_publisher(Float32, topic, 10))
        
        # Initialize state tracking
        # Store raw sensor data (e.g., Image messages) for inference
        self.latest_sensor_data: List[Any] = [None for _ in range(len(config.ai.subscribe_topics))]
        self.received_flags: List[bool] = [False for _ in range(len(config.ai.subscribe_topics))]
        
        # Thread safety for state updates
        self._mutex = threading.Lock()
        
        # Initialize subscriptions to state topics
        self._setup_subscriptions()
        
        # Call subclass-specific initialization
        self._initialize_inference()
        
        self.get_logger().info(
            f"Inference node '{node_name}' started: "
            f"{len(config.ai.publish_topics)} action outputs, "
            f"{len(config.ai.subscribe_topics)} sensor inputs."
        )
    
    def _validate_config(self) -> bool:
        """
        Validate configuration. Override in subclass for specific checks.
        """
        return True
    
    def _setup_subscriptions(self) -> None:
        """
        Set up ROS2 subscriptions to sensor topics.
        Currently supports Image topics. Override for different message types.
        """
        self.state_subscriptions = []
        for i, topic in enumerate(self.config.ai.subscribe_topics):
            # TODO: Support multiple message types based on config
            # For now, hardcoded to Image (camera sensors)
            self.state_subscriptions.append(
                self.create_subscription(Image, topic, self._make_sensor_callback(i), 10)
            )
            self.get_logger().info(f"Subscribed to sensor topic: {topic}")
    
    def _make_sensor_callback(self, sensor_index: int):
        """
        Create a callback function for a specific sensor topic.
        """
        def _callback(msg: Image):
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
            for publisher, action_value in zip(self.action_publishers, actions):
                msg = Float32()
                msg.data = float(action_value)
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
    def infer(self, sensor_data: List[Any]) -> List[float]:
        """
        Run inference on sensor data and return action values.
        
        Args:
            sensor_data: List of sensor messages (e.g., Image messages from cameras)
        
        Returns:
            List of action values (one per action publisher)
        """
        pass

