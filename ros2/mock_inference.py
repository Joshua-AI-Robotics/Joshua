import random
import sys
import random
from typing import List, Any, Dict

import rclpy

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
        """
        Initialize the mock inference node. For actual models, this would load the model and initialize it.
        """
        # Random noise generator bounds
        self.noise_low: float = -0.02
        self.noise_high: float = 0.02
        
        # Call parent constructor (sets up publishers, subscribers, etc.)
        super().__init__(node_name, node_id, config)
    
    def _validate_config(self) -> bool:
        """
        Validate the configuration.
        """
        if self.config.general.operation_mode != config_pb2.General.OperationMode.MODE_MOCK_INFERENCE:
            self.get_logger().error(
                "Mock inference node is only supported in mock inference mode."
            )
            return False
        return True

    def _process_sensor_data(self, sensor_data: List[Any]) -> Any:
        """
        Process raw sensor data into a structured format.
        
        For mock inference, we just extract basic metadata about the sensors.
        For real models (like SmolVLA), this would:
        - Convert sensor_msgs/Image to PIL Images or tensors
        - Resize and normalize images
        - Tokenize text instructions
        - Stack multi-view camera data
        
        Args:
            sensor_data: List of ROS messages (e.g., Image messages)
        
        Returns:
            Dictionary with processed sensor information
        """
        processed_data = {
            'num_sensors': len(sensor_data),
            'sensor_types': [type(sensor).__name__ for sensor in sensor_data],
            'sensor_data': [sensor.data for sensor in sensor_data],
            'sensor_timestamps': [sensor.timestamp for sensor in sensor_data],
        }

        return processed_data
        
    def _run_model_inference(self, processed_inputs: Dict[str, Any]) -> Any:
        """
        Run mock inference - generate random actions.
        
        For real models, this would:
        - Run forward pass through the model
        - Generate action predictions from processed inputs
        - Return raw model outputs (logits, action values, etc.)
        
        Args:
            processed_inputs: Output from _process_sensor_data()
        
        Returns:
            List of raw action values (one per action dimension)
        """
        num_sensors = processed_inputs['num_sensors']
        num_actions = len(self.publishers_list)

        aggregated_state = sum(processed_inputs['sensor_data']) / max(1, num_sensors)

        action_values = [aggregated_state + random.uniform(self.noise_low, self.noise_high) for _ in range(num_actions)]
        return action_values

def main(argv=None):
    return node_runner_py.run_node(MockInferencePy, logger_name="mock_inference_py", argv=argv)


if __name__ == "__main__":
    sys.exit(main())
