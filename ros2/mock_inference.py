import sys
import random
from typing import List

from sensor_msgs.msg import Image

# Protobuf generated modules
from config.proto import config_pb2

from ros2 import node_runner as node_runner_py
from ros2.inference_base import InferenceBase


class MockInferencePy(InferenceBase):
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        # Random noise generator bounds
        self.noise_low: float = -0.02
        self.noise_high: float = 0.02
        
        # Call parent constructor which handles all the setup
        super().__init__(node_name, node_id, config)

    def _is_valid_operation_mode(self, operation_mode: config_pb2.General.OperationMode) -> bool:
        """Check if the operation mode is valid for mock inference."""
        return operation_mode == config_pb2.General.OperationMode.MODE_MOCK_INFERENCE_PY
    
    def _process_image_message(self, msg: Image, state_index: int) -> float:
        """Process image message by generating a random state value."""
        del msg  # unused - we just generate random values
        return random.uniform(self.noise_low, self.noise_high)
    
    def _run_inference(self, states: List[float]) -> List[float]:
        """Run mock inference by aggregating states and adding noise."""
        if not states:
            return []
        
        # Aggregate all states and add random noise
        aggregated_state = sum(states) / max(1, len(states))
        
        # Generate actions for all publishers (same value with noise for each)
        actions = []
        for _ in range(len(self.action_publishers)):
            action_value = aggregated_state + random.uniform(self.noise_low, self.noise_high)
            actions.append(action_value)
        
        return actions


def main(argv=None):
    return node_runner_py.run_node(MockInferencePy, logger_name="mock_inference_py", argv=argv)


if __name__ == "__main__":
    sys.exit(main()) 