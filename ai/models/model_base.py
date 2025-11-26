from abc import ABC, abstractmethod
from typing import Any, List


# TODO: Add more features on base.
class ModelBase(ABC):
    def __init__(self):
        """
        Initialize the model.
        """
        pass

    @abstractmethod
    def inference(self, input_data: List[Any]) -> List[Any]:
        """
        Run inference on the input data and return the output data.
        """
        pass
