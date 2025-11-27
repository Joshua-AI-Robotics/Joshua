from abc import ABC, abstractmethod
from typing import Any, Callable, List

from ai.proto.ai_model_pb2 import SingleModel


# TODO: Add more features on base.
class ModelBase(ABC):
    """
    Abstract base class for AI models.
    Defines the interface for inference, training (forward), and input handling.
    """

    def __init__(self, config: SingleModel):
        """
        Initialize the model base.

        Args:
            config: The full SingleModel configuration which incldues node info,
            publishers, subscriptions, and model specific config.
        """
        self._single_model_config = config

        # Automatically parse the specific model config
        config_field = config.WhichOneof("model_config")
        if not config_field:
            raise ValueError("No model_config field set in SingleModel")

        # Parse the model specific config.
        self._model_config = getattr(config, config_field)

        self._validate_config()
        self._setup_inputs()

    # TODO: Rename this function (something like setup input.output).
    def _setup_inputs(self) -> None:
        """
        Initialize input tracking state.
        """
        self._num_subscriptions = len(self._single_model_config.subscriptions)
        self._num_publishers = len(self._single_model_config.pubishers)

    @abstractmethod
    def _validate_config(self) -> None:
        """
        Model specific validation should be done in the subclass.
        """
        pass

    @abstractmethod
    def handle_input(
        self,
        subscription_index: int,
        data: Any,
        publish_callback: Callable[[int, Any], None],
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
            subscription_index: Index of the ros2 subscriptions list.
            data: The received data (ROS message or value) from the subscription.
            publish_callback: Callback to publish results to ros2 publisher. Expects List[Any].
        """
        pass

    @abstractmethod
    def preprocess_input(self, subscription_index: int, data: Any) -> Any:
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
