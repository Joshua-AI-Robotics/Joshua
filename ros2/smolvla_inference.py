import sys
from typing import List, Any, Optional
import numpy as np
import torch
from PIL import Image

import rclpy
from sensor_msgs.msg import Image as ImageMsg

# Protobuf generated modules
from config.proto import config_pb2

from ros2 import node_runner as node_runner_py
from ros2.inference_base import InferenceBase
from ros2.image_converter import ImageConverter


class SmolVLAInference(InferenceBase):
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

    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        # Initialize attributes BEFORE calling super().__init__() because the parent
        # will call _initialize_inference() which needs these attributes
        self.model = None
        self.preprocessor = None
        self.postprocessor = None
        self.device = None
        self.bridge = ImageConverter() # For converting ROS images to OpenCV/PIL format
        self.task_description = self.TASK_DESCRIPTION
        
        super().__init__(node_name, node_id, config)
    

    def _validate_config(self) -> bool:
        """
        Validate that the operation mode is set correctly for smolVLA inference.
        """
        if self.config.general.operation_mode != config_pb2.General.OperationMode.MODE_INFERENCE:
            self.get_logger().error(
                "SmolVLA inference node requires MODE_SMOLVLA_INFERENCE operation mode."
            )
            return False
        
        if len(self._single_model.subscriptions) == 0:
            self.get_logger().error(
                "SmolVLA inference node requires at least one subscription."
            )
            return False
        
        return True
    
    def _initialize_inference(self) -> None:
        """
        Initialize SmolVLA model and processors.
        
        This includes:
        1. Loading the pretrained SmolVLA model
        2. Setting up pre/post-processors for data normalization and tokenization
        3. Configuring the device (CPU/CUDA)
        """
        try:
            from lerobot.policies.smolvla.modeling_smolvla import SmolVLAPolicy
            from lerobot.policies.smolvla.configuration_smolvla import SmolVLAConfig
            from lerobot.policies.smolvla.processor_smolvla import make_smolvla_pre_post_processors

            # self.device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
            self.device = torch.device("cpu")
            self.get_logger().info(f"Using device: {self.device}")

            model_name = getattr(self._single_model, 'model_name', 'HuggingFaceM4/SmolVLM-Instruct')
            pretrained_model_path = getattr(self._single_model, 'pretrained_model_path', None)

            self.get_logger().info(f"Loading SmolVLA from: {pretrained_model_path}")
            
            # Load model directly - it will load its config internally and handle the 'type' field correctly
            self.model = SmolVLAPolicy.from_pretrained(pretrained_model_path)
            
            # Update device in the loaded config
            self.model.config.device = str(self.device)
            
            # Create preprocessors/postprocessors from the model's config
            self.preprocessor, self.postprocessor = make_smolvla_pre_post_processors(config=self.model.config)

            self.model.to(self.device)
            self.model.eval()

            self.get_logger().info(f"SmolVLA inference initialized successfully. Task: '{self.task_description}'")

        except Exception as e:
            self.get_logger().error(f"Failed to initialize SmolVLA: {e}")
            import traceback
            self.get_logger().error(traceback.format_exc())
            raise


    def _run_model_inference(self, input_data: List[Any]) -> List[Any]:
        """
        Run SmolVLA inference on input camera images and language task.
        
        Args:
            input_data: List of ROS Image messages from camera subscriptions
        
        Returns:
            List of action values (one per publisher)
        """

        # Convert ROS Image messages to PIL Images
        images = []

        for i, msg in enumerate(input_data):
            if not isinstance(msg, ImageMsg):
                self.get_logger().warn(f"Input {i} is not an Image message, skipping")
                continue
            
            # Convert ROS Image to OpenCV format (RGB)
            cv_image = self.bridge.imgmsg_to_cv2(msg, desired_encoding='rgb8')
            cv_image_chw = np.transpose(cv_image, (2, 0, 1))
            cv_image_batch = np.expand_dims(cv_image_chw, axis=0)
            
            # SmolVLA preprocessor expects numpy arrays (H, W, C) in RGB format
            images.append(cv_image_batch)

        if len(images) == 0:
            self.get_logger().error("No valid images received for inference")
            return [0.0] * len(self.publishers_list)

        # Prepare input dictionary for SmolVLA
        # SmolVLA expects keys like: observation.images.camera1, observation.images.camera2, etc. (1-indexed)
        observation = {}
        for i, img in enumerate(images):
            camera_key = f"observation.images.camera{i+1}"
            observation[camera_key] = img

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
        
        with torch.no_grad():
            output = self.model(processed_batch)
        
        actions = self.postprocessor(output)
        action_tensor = actions['action']

        # Shape is typically (batch_size, action_dim) or (batch_size, chunk_size, action_dim)
        # Take the first timestep if chunk_size > 1

        action_tensor = action_tensor[0] # Remove batch dimension
        action_tensor = action_tensor[0] # take first timestamp if chunked
        action_values = action_tensor.cpu().numpy().tolist()

        num_publishers = len(self.publishers_list)
        if len(action_values) != len(self.publishers_list):
            self.get_logger().warn(
            f"Model output {len(action_values)} actions, but need {num_publishers} Padding with zeros.")
            return [0.0] * len(self.publishers_list)

        return action_values

def main(argv=None):
    return node_runner_py.run_node(SmolVLAInference, logger_name="smolvla_inference", argv=argv)


if __name__ == "__main__":
    sys.exit(main())
        
