import sys
from typing import List, Any, Optional
import numpy as np
import torch
from PIL import Image

import rclpy
from sensor_msgs.msg import Image as ImageMsg
from std_msgs.msg import Float32

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

            # Force CPU usage to avoid CUDA out of memory
            self.device = torch.device("cpu")
            self.get_logger().info(f"Using device: {self.device}")

            model_name = getattr(self._single_model, 'model_name', 'HuggingFaceM4/SmolVLM-Instruct')
            pretrained_model_path = getattr(self._single_model, 'pretrained_model_path', None)

            self.get_logger().info(f"Loading SmolVLA from: {pretrained_model_path}")
            
            # Load model
            self.model = SmolVLAPolicy.from_pretrained(pretrained_model_path)
            
            # CRITICAL: Move ALL model components to CPU explicitly
            # This ensures no tensors remain on CUDA
            self.model = self.model.to(self.device)
            
            # Update device in the loaded config
            self.model.config.device = str(self.device)
            
            # Create preprocessors/postprocessors from the model's config
            self.preprocessor, self.postprocessor = make_smolvla_pre_post_processors(config=self.model.config)

            # Set to eval mode for inference
            self.model.eval()

            self.get_logger().info(f"SmolVLA inference initialized successfully on CPU. Task: '{self.task_description}'")

        except Exception as e:
            self.get_logger().error(f"Failed to initialize SmolVLA: {e}")
            import traceback
            self.get_logger().error(traceback.format_exc())
            raise


    def _run_model_inference(self, input_data: List[Any]) -> List[Any]:
        """
        Run SmolVLA inference on input camera images, joint states, and language task.
        
        Args:
            input_data: List of ROS messages ordered as:
                - First: Image messages (cameras)
                - Rest: Float32 messages (joint encoders)
        
        Returns:
            List of action values (one per publisher)
        """

        # Separate images and encoder values
        images = []
        joint_positions = []

        for i, msg in enumerate(input_data):
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
            return [0.0] * len(self.publishers_list)

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
        
        with torch.no_grad():
            # For inference, we use the select_action method which doesn't require action labels
            output = self.model.select_action(processed_batch)
        
        # select_action returns the action tensor directly, no need for postprocessing
        action_tensor = output

        # Shape is typically (batch_size, action_dim) or (batch_size, chunk_size, action_dim)
        # Take the first timestep if chunk_size > 1
        if action_tensor.dim() == 3:
            action_tensor = action_tensor[0, 0]  # Remove batch and take first timestep
        elif action_tensor.dim() == 2:
            action_tensor = action_tensor[0]  # Just remove batch dimension
        
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
        
