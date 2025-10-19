import sys
import random
from typing import List, Any

import rclpy
from sensor_msgs.msg import Image

# Protobuf generated modules
from config.proto import config_pb2

from ros2 import node_runner as node_runner_py
from ros2.inference_base import InferenceBase


class MockInferencePy(InferenceBase):
    """
    Mock inference node that generates random actions.
    
    This is a simple implementation for testing the inference pipeline
    without requiring a trained model. It generates random noise-based
    actions in response to sensor inputs.
    """
    
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        # Random noise generator bounds
        self.noise_low: float = -0.02
        self.noise_high: float = 0.02
        
        # Call parent constructor (sets up publishers, subscribers, etc.)
        super().__init__(node_name, node_id, config)
    
    def _validate_config(self) -> bool:
        """
        Validate that the operation mode is set correctly for mock inference.
        """
        if self.config.general.operation_mode != config_pb2.General.OperationMode.MODE_MOCK_INFERENCE_PY:
            self.get_logger().error(
                "Mock inference node requires MODE_MOCK_INFERENCE_PY operation mode."
            )
            return False
        return True
    
    def _initialize_inference(self) -> None:
        """
        Initialize mock inference (no model to load).
        """
        self.get_logger().info(
            f"Mock inference initialized with noise range: [{self.noise_low}, {self.noise_high}]"
        )
    
    def infer(self, sensor_data: List[Any]) -> List[float]:
        """
        Generate random actions based on sensor inputs.
        
        For mock inference, we:
        1. Generate a random value for each sensor input
        2. Aggregate them (simple average)
        3. Add noise to create one action per output
        
        Args:
            sensor_data: List of sensor messages (currently Image messages)
        
        Returns:
            List of random action values
        """
        # Generate random values for each sensor
        sensor_values = [
            random.uniform(self.noise_low, self.noise_high) 
            for _ in sensor_data
        ]
        
        # Aggregate sensor values (simple average)
        aggregated = sum(sensor_values) / max(1, len(sensor_values))
        
        # Generate one action per publisher
        actions = [
            aggregated + random.uniform(self.noise_low, self.noise_high)
            for _ in self.action_publishers
        ]
        
        return actions


def main(argv=None):
    return node_runner_py.run_node(MockInferencePy, logger_name="mock_inference_py", argv=argv)


if __name__ == "__main__":
    sys.exit(main()) 