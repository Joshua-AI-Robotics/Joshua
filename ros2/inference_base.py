import threading
from abc import ABC, abstractmethod
from typing import List

import rclpy
from rclpy.node import Node
from std_msgs.msg import Float32
from sensor_msgs.msg import Image

# Protobuf generated modules
from config.proto import config_pb2
from config.proto import ai_pb2


class InferenceBase(Node, ABC):
    """Abstract base class for inference nodes.
    
    This class provides the common infrastructure for inference nodes including:
    - ROS2 node initialization
    - Publisher/subscriber setup
    - State tracking
    - Thread-safe message handling
    
    Subclasses must implement the inference logic by overriding abstract methods.
    """
    
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        super().__init__(node_name)
        
        self.node_id = node_id
        self.config = config
        
        # Validate operation mode (subclasses can override this check)
        if not self._is_valid_operation_mode(config.general.operation_mode):
            self.get_logger().error(f"Invalid operation mode for {self.__class__.__name__}")
            return
        
        # Initialize publishers
        self.action_publishers: List[rclpy.publisher.Publisher] = []
        for i in range(len(config.ai.publish_topics)):
            topic = config.ai.publish_topics[i]
            self.action_publishers.append(self.create_publisher(Float32, topic, 10))
        
        # Initialize state tracking
        self.latest_states: List[float] = [0.0 for _ in range(len(config.ai.subscribe_topics))]
        self.received: List[bool] = [False for _ in range(len(config.ai.subscribe_topics))]
        
        # Thread safety
        self._mutex = threading.Lock()
        
        # Setup subscriptions
        self._setup_subscriptions()
        
        self.get_logger().info(
            f"{self.__class__.__name__} started ({len(config.ai.publish_topics)} actions, {len(config.ai.subscribe_topics)} states)."
        )
    
    def _setup_subscriptions(self) -> None:
        """Setup ROS2 subscriptions for state topics."""
        self.state_subscriptions = []
        for i in range(len(self.config.ai.subscribe_topics)):
            topic = self.config.ai.subscribe_topics[i]
            # Note: message type is currently hardcoded to Image
            self.state_subscriptions.append(
                self.create_subscription(Image, topic, self._make_image_cb(i), 10)
            )
            self.get_logger().info(f"Subscribed to image state topic: {topic}")
    
    def _make_image_cb(self, state_index: int):
        """Create callback function for image messages."""
        def _cb(msg: Image):
            with self._mutex:
                if state_index >= len(self.latest_states):
                    self.get_logger().warn(f"Received state index out of range: {state_index}")
                    return
                
                # Process the incoming message and update state
                self.latest_states[state_index] = self._process_image_message(msg, state_index)
                self.received[state_index] = True
                
                # If we have a fresh message from all topics, run inference
                if all(self.received):
                    self._run_inference_and_publish()
                    self.received = [False for _ in self.received]
        return _cb
    
    def _run_inference_and_publish(self) -> None:
        """Run inference on current states and publish actions."""
        if not self.latest_states:
            return
        
        # Run inference to get actions
        actions = self._run_inference(self.latest_states.copy())
        
        # Publish actions
        self._publish_actions(actions)
    
    def _publish_actions(self, actions: List[float]) -> None:
        """Publish action values to ROS2 topics."""
        if len(actions) != len(self.action_publishers):
            self.get_logger().warn(
                f"Action count mismatch: got {len(actions)}, expected {len(self.action_publishers)}"
            )
            return
        
        for i, publisher in enumerate(self.action_publishers):
            msg = Float32()
            msg.data = actions[i] if i < len(actions) else 0.0
            publisher.publish(msg)
    
    @abstractmethod
    def _is_valid_operation_mode(self, operation_mode: config_pb2.General.OperationMode) -> bool:
        """Check if the given operation mode is valid for this inference node.
        
        Args:
            operation_mode: The operation mode from config
            
        Returns:
            True if the operation mode is valid for this node type
        """
        pass
    
    @abstractmethod
    def _process_image_message(self, msg: Image, state_index: int) -> float:
        """Process an incoming image message and extract state value.
        
        Args:
            msg: The ROS2 Image message
            state_index: Index of the state being updated
            
        Returns:
            The processed state value as a float
        """
        pass
    
    @abstractmethod
    def _run_inference(self, states: List[float]) -> List[float]:
        """Run inference on the current states to generate actions.
        
        Args:
            states: List of current state values
            
        Returns:
            List of action values to publish
        """
        pass
