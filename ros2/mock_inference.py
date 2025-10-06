import sys
import random
from typing import List, Dict, Any

from sensor_msgs.msg import Image

# Protobuf generated modules
from config.proto import config_pb2

from ros2 import node_runner as node_runner_py
from ros2.inference_base import InferenceBase


class MockInferencePy(InferenceBase):
    """Mock inference node that generates random actions based on observation data.
    
    This serves as a simple baseline policy that:
    - Processes camera images by extracting basic statistics
    - Uses encoder states as input features
    - Generates random actions with some correlation to inputs
    - Demonstrates the observation-based inference interface
    """
    
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        # Random noise generator bounds
        self.noise_low: float = -0.02
        self.noise_high: float = 0.02
        
        # Call parent constructor which handles all the setup
        super().__init__(node_name, node_id, config)

    def _is_valid_operation_mode(self, operation_mode: config_pb2.General.OperationMode) -> bool:
        """Check if the operation mode is valid for mock inference."""
        return operation_mode == config_pb2.General.OperationMode.MODE_MOCK_INFERENCE_PY
    
    def _run_inference_from_observation(self, observation: Dict[str, Any]) -> List[float]:
        """Run mock inference using the observation dictionary.
        
        Args:
            observation: Dictionary containing:
                - "images": List[Image] - Camera image data
                - "state": List[float] - Encoder/joint state data
                
        Returns:
            List of action values to publish
        """
        # Extract data from observation
        camera_data = observation.get("images", [])
        encoder_states = observation.get("state", [])
        
        # Process camera data to extract simple features
        camera_features = self._extract_camera_features(camera_data)
        
        # Combine camera features and encoder states
        all_features = camera_features + encoder_states
        
        if not all_features:
            # If no data available, return small random actions
            return [random.uniform(self.noise_low, self.noise_high) for _ in range(len(self.action_publishers))]
        
        # Aggregate all features
        aggregated_feature = sum(all_features) / max(1, len(all_features))
        
        # Generate actions for all publishers (same base value with noise for each)
        actions = []
        for _ in range(len(self.action_publishers)):
            action_value = aggregated_feature * 0.1 + random.uniform(self.noise_low, self.noise_high)
            actions.append(action_value)
        
        self.get_logger().debug(f"Mock inference: {len(camera_data)} images, {len(encoder_states)} states -> {len(actions)} actions")
        return actions
    
    def _extract_camera_features(self, camera_data: List[Image]) -> List[float]:
        """Extract simple features from camera images for mock processing.
        
        Args:
            camera_data: List of camera Image messages
            
        Returns:
            List of extracted features (mock values for now)
        """
        features = []
        
        for img_msg in camera_data:
            # For mock inference, we'll just generate pseudo-features based on image properties
            # In a real implementation, you'd process the actual image data
            
            # Use image dimensions and timestamp as pseudo-features
            width_feature = (img_msg.width / 1000.0) * random.uniform(0.8, 1.2)
            height_feature = (img_msg.height / 1000.0) * random.uniform(0.8, 1.2)
            
            # Add some randomness based on image encoding
            encoding_hash = hash(img_msg.encoding) % 1000
            encoding_feature = (encoding_hash / 1000.0) * random.uniform(0.5, 1.5)
            
            features.extend([width_feature, height_feature, encoding_feature])
        
        return features


def main(argv=None):
    return node_runner_py.run_node(MockInferencePy, logger_name="mock_inference_py", argv=argv)


if __name__ == "__main__":
    sys.exit(main()) 