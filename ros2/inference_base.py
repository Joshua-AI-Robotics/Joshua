import threading
from abc import ABC, abstractmethod
from typing import List, Union, Dict, Any

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
    - State tracking for both camera and encoder data
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
        
        # Separate camera and encoder topics for better organization
        self.camera_topics = []
        self.encoder_topics = []
        
        # Parse subscribe topics to separate camera and encoder data
        for i, topic in enumerate(config.ai.subscribe_topics):
            if "camera" in topic.lower():
                self.camera_topics.append((i, topic))
            else:
                # Assume encoder/state data
                self.encoder_topics.append((i, topic))
        
        # Initialize state tracking
        self.latest_camera_data: List[Union[Image, None]] = [None for _ in self.camera_topics]
        self.latest_encoder_states: List[float] = [0.0 for _ in self.encoder_topics]
        self.received_camera: List[bool] = [False for _ in self.camera_topics]
        self.received_encoder: List[bool] = [False for _ in self.encoder_topics]
        
        # Thread safety
        self._mutex = threading.Lock()
        
        # Setup subscriptions
        self._setup_subscriptions()
        
        self.get_logger().info(
            f"{self.__class__.__name__} started ({len(config.ai.publish_topics)} actions, "
            f"{len(self.camera_topics)} cameras, {len(self.encoder_topics)} encoders)."
        )
    
    def _setup_subscriptions(self) -> None:
        """Setup ROS2 subscriptions for camera and encoder topics."""
        self.camera_subscriptions = []
        self.encoder_subscriptions = []
        
        # Subscribe to camera topics
        for idx, (original_idx, topic) in enumerate(self.camera_topics):
            self.camera_subscriptions.append(
                self.create_subscription(Image, topic, self._make_camera_cb(idx), 10)
            )
            self.get_logger().info(f"Subscribed to camera topic: {topic}")
        
        # Subscribe to encoder topics
        for idx, (original_idx, topic) in enumerate(self.encoder_topics):
            self.encoder_subscriptions.append(
                self.create_subscription(Float32, topic, self._make_encoder_cb(idx), 10)
            )
            self.get_logger().info(f"Subscribed to encoder topic: {topic}")
    
    def _make_camera_cb(self, camera_index: int):
        """Create callback function for camera messages."""
        def _cb(msg: Image):
            with self._mutex:
                if camera_index >= len(self.latest_camera_data):
                    self.get_logger().warn(f"Received camera index out of range: {camera_index}")
                    return
                
                self.latest_camera_data[camera_index] = msg
                self.received_camera[camera_index] = True
                
                # Check if we have fresh data from all sources
                self._check_and_run_inference()
        return _cb
    
    def _make_encoder_cb(self, encoder_index: int):
        """Create callback function for encoder messages."""
        def _cb(msg: Float32):
            with self._mutex:
                if encoder_index >= len(self.latest_encoder_states):
                    self.get_logger().warn(f"Received encoder index out of range: {encoder_index}")
                    return
                
                self.latest_encoder_states[encoder_index] = msg.data
                self.received_encoder[encoder_index] = True
                
                # Check if we have fresh data from all sources
                self._check_and_run_inference()
        return _cb
    
    def _check_and_run_inference(self) -> None:
        """Check if we have fresh data from all sources and run inference if so."""
        # Check if we have fresh data from all cameras and encoders
        all_camera_received = len(self.received_camera) == 0 or all(self.received_camera)
        all_encoder_received = len(self.received_encoder) == 0 or all(self.received_encoder)
        
        if all_camera_received and all_encoder_received:
            self._run_inference_and_publish()
            # Reset received flags
            self.received_camera = [False for _ in self.received_camera]
            self.received_encoder = [False for _ in self.received_encoder]
    
    def _run_inference_and_publish(self) -> None:
        """Run inference on current camera and encoder data and publish actions."""
        try:
            # Filter out None camera data
            valid_camera_data = [img for img in self.latest_camera_data if img is not None]
            
            # Create observation dictionary
            observation = self._create_observation(valid_camera_data, self.latest_encoder_states.copy())
            
            # Run inference with observation structure
            actions = self._run_inference_from_observation(observation)
            
            # Publish actions
            self._publish_actions(actions)
            
        except Exception as e:
            self.get_logger().error(f"Error during inference and publishing: {str(e)}")
    
    def _create_observation(self, camera_data: List[Image], encoder_states: List[float]) -> Dict[str, Any]:
        """Create observation dictionary from camera and encoder data.
        
        Args:
            camera_data: List of camera Image messages
            encoder_states: List of encoder state values
            
        Returns:
            Observation dictionary with standardized structure
        """
        observation = {
            "images": camera_data,
            "state": encoder_states,
            # Future extensions can add:
            # "depth": depth_images,
            # "force": force_readings,
            # "audio": audio_data,
            # etc.
        }
        return observation
    
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
    def _run_inference_from_observation(self, observation: Dict[str, Any]) -> List[float]:
        """Run inference on the observation to generate actions.
        
        This is the main inference method that subclasses must implement.
        The observation dictionary contains all sensory inputs in a structured format.
        
        Args:
            observation: Dictionary containing:
                - "images": List[Image] - Camera image data
                - "state": List[float] - Encoder/joint state data
                - Additional keys may be added in the future
                
        Returns:
            List of action values to publish
        """
        pass
    
    def _run_inference(self, camera_data: List[Image], encoder_states: List[float]) -> List[float]:
        """Convenience method for backward compatibility.
        
        This method creates an observation dictionary and calls the main inference method.
        Subclasses can override this if they need custom behavior, but typically should
        implement _run_inference_from_observation instead.
        
        Args:
            camera_data: List of camera Image messages
            encoder_states: List of current encoder state values
            
        Returns:
            List of action values to publish
        """
        observation = self._create_observation(camera_data, encoder_states)
        return self._run_inference_from_observation(observation)
