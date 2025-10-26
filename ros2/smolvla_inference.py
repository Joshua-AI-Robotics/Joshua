"""
SmolVLA Inference Node for Project Joshua

This node integrates the SmolVLA (Small Vision-Language-Action) model
from HuggingFace LeRobot for robotic manipulation tasks.

SmolVLA is a vision-language-action model that processes images and
optional text instructions to generate robot action commands.
"""

import sys
import os
from typing import List, Any, Optional
import numpy as np

# Force CPU mode if CUDA libraries are not available
# This prevents PyTorch from trying to load CUDA libraries that may not be in the system path
os.environ.setdefault('CUDA_VISIBLE_DEVICES', '')

try:
    import torch
    from transformers import AutoProcessor, AutoModelForVision2Seq
    from PIL import Image as PILImage
except ImportError as e:
    print(f"Error importing required packages: {e}")
    print("Please install: pip install torch transformers pillow")
    sys.exit(1)

from sensor_msgs.msg import Image

# Protobuf generated modules
from config.proto import config_pb2

from ros2 import node_runner as node_runner_py
from ros2.inference_base import InferenceBase


class SmolVLAInference(InferenceBase):
    """
    SmolVLA inference node for vision-language-action learning.
    
    This node loads a pretrained SmolVLA model and uses it to generate
    robot actions based on camera observations and optional task instructions.
    """
    
    def __init__(self, node_name: str, node_id: int, config: config_pb2.Config):
        # Model configuration - get from config or use defaults
        self.model_checkpoint = self._get_checkpoint_path(config)
        self.device = "cuda" if torch.cuda.is_available() else "cpu"
        self.use_fp16 = torch.cuda.is_available()  # Use bfloat16 on GPU
        
        # Task instruction (can be empty or from config)
        self.task_instruction = self._get_task_instruction(config)
        
        # Model components (initialized in _initialize_inference)
        self.model = None
        self.processor = None
        
        # Action bounds (optional, for clipping/normalization)
        self.action_min = -1.0
        self.action_max = 1.0
        
        # Performance tracking
        self.inference_count = 0
        
        # Call parent constructor (sets up ROS2 infrastructure)
        super().__init__(node_name, node_id, config)
    
    def _get_checkpoint_path(self, config: config_pb2.Config) -> str:
        """
        Get model checkpoint path from config or use default.
        """
        # TODO: Add model_checkpoint_path field to ai.proto if not present
        # For now, try to get from config or use default HF model
        
        # Check if config has the field (you may need to add this to proto)
        if hasattr(config.ai, 'model_checkpoint_path') and config.ai.model_checkpoint_path:
            return config.ai.model_checkpoint_path
        
        # Default to HuggingFace model (requires internet connection)
        return "HuggingFaceH4/SmolVLA-1.7B"
    
    def _get_task_instruction(self, config: config_pb2.Config) -> str:
        """
        Get task instruction from config or use default.
        """
        # TODO: Add task_instruction field to ai.proto if needed
        if hasattr(config.ai, 'task_instruction') and config.ai.task_instruction:
            return config.ai.task_instruction
        
        # Empty instruction by default
        return ""
    
    def _validate_config(self) -> bool:
        """
        Validate configuration for SmolVLA inference.
        """
        # Check operation mode if you have a specific mode for SmolVLA
        # For now, we'll be more flexible
        
        if not self.config.ai.subscribe_topics:
            self.get_logger().error("No subscribe_topics configured for SmolVLA inference")
            return False
        
        if not self.config.ai.publish_topics:
            self.get_logger().error("No publish_topics configured for SmolVLA inference")
            return False
        
        return True
    
    def _initialize_inference(self) -> None:
        """
        Load the SmolVLA model and processor.
        """
        self.get_logger().info(f"Loading SmolVLA model from: {self.model_checkpoint}")
        self.get_logger().info(f"Using device: {self.device}")
        
        try:
            # Load processor (handles image preprocessing and tokenization)
            self.get_logger().info("Loading processor...")
            self.processor = AutoProcessor.from_pretrained(
                self.model_checkpoint,
                trust_remote_code=True
            )
            
            # Load model
            self.get_logger().info("Loading model...")
            dtype = torch.bfloat16 if self.use_fp16 else torch.float32
            
            self.model = AutoModelForVision2Seq.from_pretrained(
                self.model_checkpoint,
                torch_dtype=dtype,
                device_map="auto" if self.device == "cuda" else None,
                trust_remote_code=True
            )
            
            if self.device == "cpu":
                self.model = self.model.to(self.device)
            
            self.model.eval()
            
            self.get_logger().info("SmolVLA model loaded successfully!")
            if self.task_instruction:
                self.get_logger().info(f"Task instruction: '{self.task_instruction}'")
            
        except Exception as e:
            self.get_logger().error(f"Failed to load SmolVLA model: {str(e)}")
            raise
    
    def _ros_image_to_pil(self, ros_image: Image) -> PILImage.Image:
        """
        Convert ROS2 Image message to PIL Image.
        
        Args:
            ros_image: ROS2 Image message
        
        Returns:
            PIL Image object
        """
        # Get image dimensions
        height = ros_image.height
        width = ros_image.width
        encoding = ros_image.encoding
        
        # Convert based on encoding
        if encoding == "rgb8":
            # RGB8: 3 channels, 8 bits per channel
            img_array = np.frombuffer(ros_image.data, dtype=np.uint8)
            img_array = img_array.reshape((height, width, 3))
            return PILImage.fromarray(img_array, mode='RGB')
        
        elif encoding == "bgr8":
            # BGR8: 3 channels, need to convert to RGB
            img_array = np.frombuffer(ros_image.data, dtype=np.uint8)
            img_array = img_array.reshape((height, width, 3))
            # Convert BGR to RGB
            img_array = img_array[:, :, ::-1]
            return PILImage.fromarray(img_array, mode='RGB')
        
        elif encoding == "mono8":
            # Grayscale: convert to RGB by repeating channel
            img_array = np.frombuffer(ros_image.data, dtype=np.uint8)
            img_array = img_array.reshape((height, width))
            # Convert to RGB
            img_array = np.stack([img_array] * 3, axis=-1)
            return PILImage.fromarray(img_array, mode='RGB')
        
        elif encoding == "rgba8":
            # RGBA8: drop alpha channel
            img_array = np.frombuffer(ros_image.data, dtype=np.uint8)
            img_array = img_array.reshape((height, width, 4))
            # Drop alpha, keep RGB
            img_array = img_array[:, :, :3]
            return PILImage.fromarray(img_array, mode='RGB')
        
        else:
            self.get_logger().warn(f"Unsupported encoding: {encoding}, attempting rgb8")
            img_array = np.frombuffer(ros_image.data, dtype=np.uint8)
            img_array = img_array.reshape((height, width, -1))
            if img_array.shape[2] >= 3:
                return PILImage.fromarray(img_array[:, :, :3], mode='RGB')
            else:
                raise ValueError(f"Cannot convert encoding {encoding} to RGB")
    
    def infer(self, sensor_data: List[Any]) -> List[float]:
        """
        Run SmolVLA inference on sensor data to generate robot actions.
        
        Args:
            sensor_data: List of ROS2 Image messages from cameras
        
        Returns:
            List of action values (one per actuator)
        """
        try:
            # Convert ROS images to PIL images
            pil_images = []
            for i, ros_image in enumerate(sensor_data):
                try:
                    pil_img = self._ros_image_to_pil(ros_image)
                    pil_images.append(pil_img)
                except Exception as e:
                    self.get_logger().error(f"Failed to convert image {i}: {str(e)}")
                    raise
            
            # Prepare inputs using processor
            # SmolVLA can take multiple images and optional text
            if self.task_instruction:
                inputs = self.processor(
                    images=pil_images,
                    text=self.task_instruction,
                    return_tensors="pt"
                )
            else:
                inputs = self.processor(
                    images=pil_images,
                    return_tensors="pt"
                )
            
            # Move inputs to device
            inputs = {k: v.to(self.device) for k, v in inputs.items()}
            
            # Run inference
            with torch.no_grad():
                outputs = self.model(**inputs)
                
                # Extract actions from model output
                # The exact output format depends on SmolVLA's architecture
                # Typically, VLA models output action logits or direct action values
                if hasattr(outputs, 'action_logits'):
                    action_tensor = outputs.action_logits
                elif hasattr(outputs, 'logits'):
                    action_tensor = outputs.logits
                else:
                    # Fallback: use the last hidden state
                    action_tensor = outputs.last_hidden_state
                
                # Convert to actions
                # Assuming the model outputs continuous actions
                actions = action_tensor.squeeze()
                
                # If model outputs more dimensions, take what we need
                if actions.dim() > 1:
                    # Take mean over sequence dimension or last timestep
                    actions = actions[-1] if actions.dim() == 2 else actions.mean(dim=0)
                
                # Convert to CPU and numpy
                actions = actions.cpu().numpy()
                
                # Flatten to list
                if actions.ndim > 1:
                    actions = actions.flatten()
                
                action_list = actions.tolist()
            
            # Ensure we return the correct number of actions
            expected_actions = len(self.action_publishers)
            
            if len(action_list) < expected_actions:
                self.get_logger().warn(
                    f"Model returned {len(action_list)} actions, but expected {expected_actions}. Padding with zeros."
                )
                action_list.extend([0.0] * (expected_actions - len(action_list)))
            elif len(action_list) > expected_actions:
                self.get_logger().warn(
                    f"Model returned {len(action_list)} actions, but expected {expected_actions}. Truncating."
                )
                action_list = action_list[:expected_actions]
            
            # Clip actions to valid range
            action_list = [
                np.clip(action, self.action_min, self.action_max)
                for action in action_list
            ]
            
            # Log periodically
            self.inference_count += 1
            if self.inference_count % 100 == 0:
                self.get_logger().info(
                    f"Inference #{self.inference_count}: "
                    f"Actions = {[f'{a:.3f}' for a in action_list]}"
                )
            
            return action_list
            
        except Exception as e:
            self.get_logger().error(f"Inference failed: {str(e)}")
            # Return safe zero actions on error
            return [0.0] * len(self.action_publishers)


def main(argv=None):
    """
    Main entry point for the SmolVLA inference node.
    """
    return node_runner_py.run_node(
        SmolVLAInference,
        logger_name="smolvla_inference",
        argv=argv
    )


if __name__ == "__main__":
    sys.exit(main())

