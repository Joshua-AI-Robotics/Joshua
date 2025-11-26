from abc import ABC, abstractmethod
from typing import Any, List, Callable


# TODO: Add more features on base.
class ModelBase(ABC):
    """
    Abstract base class for AI models.
    Defines the interface for inference, training (forward), and input handling.
    """

    @abstractmethod
    def setup_inputs(self, num_inputs: int) -> None:
        """
        Initialize input tracking state.
        Args:
            num_inputs: Number of input subscriptions.
        """
        pass

    @abstractmethod
    def handle_input(
        self,
        input_index: int,
        data: Any,
        publish_callback: Callable[[List[Any]], None],
    ) -> None:
        """
        Main entry point for handling incoming data from a ros2 subscriber.
        
        This method should orchestrate:
        1. Preprocessing (preprocess_input)
        2. Synchronization/Buffering
        3. Inference (inference)
        4. Postprocessing (postprocess_output)
        5. Publishing (publish_callback)

        Args:
            input_index: Index of the input ros2 subscriber.
            data: The received data (ROS message or value).
            publish_callback: Callback to publish results to ros2 publisher. Expects List[Any].
        """
        pass

    @abstractmethod
    def preprocess_input(self, input_index: int, data: Any) -> Any:
        """
        Preprocess a single input item.
        """
        pass

    @abstractmethod
    def postprocess_output(self, output_data: List[Any]) -> List[Any]:
        """
        Postprocess the inference output before publishing.
        """
        pass

    @abstractmethod
    def inference(self, input_data: List[Any]) -> List[Any]:
        """
        Run inference on the input data and return the output data.
        """
        pass

    @abstractmethod
    def forward(self, input_data: List[Any]) -> List[Any]:
        """
        Training forward pass of the model and compute the loss.
        """
        pass
