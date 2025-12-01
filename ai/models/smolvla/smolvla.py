import sys
import threading
from typing import List, Any, Optional, Callable
import numpy as np
import torch
from PIL import Image

import rclpy
from sensor_msgs.msg import Image as ImageMsg
from std_msgs.msg import Float32

# Protobuf generated modules
from ai.proto.ai_model_pb2 import SingleModel
from ai.models.model_base import ModelBase

from ros2 import node_runner as node_runner_py
from ros2.image_converter import ImageConverter

from lerobot.policies.smolvla.modeling_smolvla import SmolVLAPolicy
from lerobot.policies.smolvla.configuration_smolvla import SmolVLAConfig
from lerobot.policies.smolvla.processor_smolvla import make_smolvla_pre_post_processors


class SmolVla(ModelBase):
    """
    SmolVLA inference node for vision-language-action policy.
    
    This implementation integrates HuggingFace's SmolVLA model from the lerobot library.
    It processes multi-camera observations and language task instructions to generate
    robot actions.
    
    References:
    - https://github.com/huggingface/lerobot/blob/main/src/lerobot/policies/smolvla/
    - Pretrained model: lerobot/smolvla_base (18k+ downloads)
    """

    # SmolVLA Configuration (hardcoded for now)
    TASK_DESCRIPTION = "Pick up the object and place it in the container."

    def __init__(self, config: SingleModel):
        super().__init__(config)

        # Initialize buffer to hold a list of values per subscription
        self._input_buffer: List[List[Any]] = [
            [] for _ in range(self._num_subscriptions)
        ]
        self._mutex = threading.Lock()

        self.model = None
        self.preprocessor = None
        self.postprocessor = None
        self.device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        self.bridge = ImageConverter()
        self.task_description = self.TASK_DESCRIPTION

    def _validate_config(self) -> None:
        """
        Validate the model configuration.
        """
        
        # "SmolVLA inference node requires at least one subscription."
        if self._num_subscriptions < 1:
            raise ValueError("SmolVLA inference node requires at least one subscription.")

        # "SmolVLA inference node requires a task description."
        if not self.task_description:
            raise ValueError("SmolVLA inference node requires a task description.")

        # "SmolVLA inference node requires a model path."
        if not self._single_model_config.pretrained_model_path:
            raise ValueError("SmolVLA inference node requires a pretrained model path.")

    def _initialize_inference(self) -> None:
        """
        Initialize SmolVLA model and processors.
        
        This includes:
        1. Loading the pretrained SmolVLA model
        2. Setting up pre/post-processors for data normalization and tokenization
        """
        try:
            model_name = getattr(self._model_config, 'model_name', 'HuggingFaceM4/SmolVLM-Instruct')
            pretrained_model_path = self._single_model_config.pretrained_model_path

            self.get_logger().info(f"Loading SmolVLA from: {pretrained_model_path}")
            
            # Load model
            self.model = SmolVLAPolicy.from_pretrained(pretrained_model_path)
            
            # CRITICAL: Move ALL model components to device
            # This ensures tensors are on the correct device
            self.model = self.model.to(self.device)
            
            # Update device in the loaded config
            self.model.config.device = str(self.device)
            
            # Create preprocessors/postprocessors from the model's config
            self.preprocessor, self.postprocessor = make_smolvla_pre_post_processors(config=self.model.config)

            # Set to eval mode for inference
            self.model.eval()

            self.get_logger().info(f"SmolVLA inference initialized successfully on {self.device}. Task: '{self.task_description}'")

        except Exception as e:
            self.get_logger().error(f"Failed to initialize SmolVLA: {e}")
            import traceback
            self.get_logger().error(traceback.format_exc())
            raise

    def handle_input(self, subscription_index: int, data: Any, publish_callback: Callable[[int, Any], None]) -> None:
        """
        Handle input data for SmolVLA inference.
        """
        processed_data = self.preprocess_input(subscription_index, data)
        with self._mutex:
            if subscription_index >= self._num_subscriptions:
                return

            self._input_buffer[subscription_index].append(processed_data)

            if len(self._input_buffer[subscription_index]) == 30:
                outputs = self.inference(self._input_buffer[subscription_index])
                final_outputs = self.postprocess_output(outputs)
                for publisher_index in range(self._num_publishers):
                    publish_callback(publisher_index, final_outputs[publisher_index])

                self._input_buffer[subscription_index] = []

    def preprocess_input(self, subscription_index: int, data: Any) -> Any:
        """
        Preprocess input data for SmolVLA inference.
        """
        images = []
        joint_positions = []

        for i, msg in enumerate(data):
            if isinstance(msg, ImageMsg):
                # Convert ROS Image to OpenCV format (RGB)
                cv_image = self.bridge.imgmsg_to_cv2(msg, desired_encoding='rgb8')
                cv_image_chw = np.transpose(cv_image, (2, 0, 1))
                cv_image_batch = np.expand_dims(cv_image_chw, axis=0)
                images.append(cv_image_batch)
            elif isinstance(msg, Float32):
                # Extract encoder value (normalized to [-1, 1] by encoder publisher)
                joint_positions.append(msg.data)
            else:
                self.get_logger().warn(f"Input {i} is unexpected type {type(msg)}, skipping")

        if len(images) == 0:
            self.get_logger().error("No valid images received for inference")
            return [0.0] * self._num_publishers

        # Assemble joint state vector
        if len(joint_positions) > 0:
            state = np.array(joint_positions, dtype=np.float32).reshape(1, -1)
            self.get_logger().info(f"Using real joint state: {state.tolist()}")
        else:
            # Fallback to dummy state if no encoders available
            state = np.zeros((1, 6), dtype=np.float32)
            self.get_logger().warn("No encoder data available, using dummy state")

        # Prepare input dictionary for SmolVLA
        # SmolVLA expects keys like: observation.images.camera1, observation.images.camera2, etc. (1-indexed)
        observation = {}
        for i, img in enumerate(images):
            camera_key = f"observation.images.camera{i+1}"
            observation[camera_key] = img

        # Add joint state
        observation["observation.state"] = state

        task = self.task_description
        if not task.endswith("\n"):
            task = f"{task}\n"

        batch = {
            **observation,
            "task": task,
        }

        self.get_logger().info(f"Batch keys before preprocessing: {list(batch.keys())}")
        
        try:
            processed_batch = self.preprocessor(batch)
        except Exception as e:
            self.get_logger().error(f"Preprocessing failed: {e}")
            self.get_logger().error(f"Batch keys: {list(batch.keys())}")
            self.get_logger().error(f"Image keys in batch: {[k for k in batch.keys() if 'image' in k.lower()]}")
            raise

        return processed_batch

    def postprocess_output(self, output_data: List[Any]) -> List[Any]:
        """
        Postprocess output data for SmolVLA inference.
        """
        # select_action returns the action tensor directly, no need for postprocessing
        action_tensor = output_data

        # Shape is typically (batch_size, action_dim) or (batch_size, chunk_size, action_dim)
        # Take the first timestep if chunk_size > 1
        if action_tensor.dim() == 3:
            action_tensor = action_tensor[0, 0]  # Remove batch and take first timestep
        elif action_tensor.dim() == 2:
            action_tensor = action_tensor[0]  # Just remove batch dimension

        if self.device.type == "cpu":
            action_values = action_tensor.cpu().numpy().tolist()
        else:
            action_values = action_tensor.numpy().tolist()

        if len(action_values) != self._num_publishers:
            self.get_logger().warn(
                f"Model output {len(action_values)} actions, but need {self._num_publishers} Padding with zeros.")
            return [0.0] * self._num_publishers

        return action_values

    def inference(self, input_data: List[Any]) -> List[Any]:
        """
        Run inference on the input data and return the output data.
        """
        outputs = self.model.select_action(input_data)
        return outputs

    def forward(self, input_data: List[Any]) -> List[Any]:
        """
        Training forward pass. Not implemented for SmolVLA.
        """
        return []