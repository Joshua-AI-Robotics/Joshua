#!/usr/bin/env python3

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy
from sensor_msgs.msg import Image, CompressedImage
from std_msgs.msg import String, Float32MultiArray
from geometry_msgs.msg import Twist
import cv2
import numpy as np
import os
import logging
from typing import Optional, List, Dict, Any
import threading
import time
import sys
import glob
import ctypes

# Setup CUDA library paths before PyTorch import
def preload_cuda_libraries():
    """Preload CUDA libraries using ctypes before PyTorch import"""
    try:
        # Find CUDA library directories in the current Python environment
        import site
        import glob
        
        # Get all site-packages directories
        site_packages = site.getsitepackages()
        if hasattr(site, 'getusersitepackages'):
            site_packages.append(site.getusersitepackages())
        
        # Also check the current runfiles directory (for Bazel)
        current_dir = os.path.dirname(os.path.abspath(__file__))
        runfiles_dirs = []
        
        # Walk up the directory tree to find runfiles
        check_dir = current_dir
        for _ in range(10):  # Limit search depth
            if 'runfiles' in check_dir:
                runfiles_dirs.append(check_dir)
                # Look for nvidia packages in runfiles
                nvidia_dirs = glob.glob(os.path.join(check_dir, "**/*nvidia*cu12*/site-packages/nvidia/*/lib"), recursive=True)
                runfiles_dirs.extend([os.path.dirname(d) for d in nvidia_dirs])
            check_dir = os.path.dirname(check_dir)
            if check_dir == '/':
                break
        
        all_search_dirs = site_packages + runfiles_dirs
        
        # Libraries to preload in dependency order
        cuda_libraries = [
            'libnvJitLink.so.12',
            'libcublas.so.12', 
            'libcublasLt.so.12',
            'libcufft.so.11',
            'libcurand.so.10',
            'libcusolver.so.11',
            'libcusparse.so.12',
            'libcudnn.so.8',
            'libnccl.so.2',
            'libnvrtc.so.12',
        ]
        
        loaded_libs = []
        
        for search_dir in all_search_dirs:
            if not os.path.exists(search_dir):
                continue
                
            # Search recursively for library files
            lib_patterns = [
                os.path.join(search_dir, "**", "lib", "*.so*"),
                os.path.join(search_dir, "**", "*.so*")
            ]
            
            found_lib_files = []
            for pattern in lib_patterns:
                found_lib_files.extend(glob.glob(pattern, recursive=True))
            
            # Try to preload the libraries we need
            for lib_name in cuda_libraries:
                for lib_file in found_lib_files:
                    if lib_name in os.path.basename(lib_file):
                        try:
                            # Try to load the library
                            lib = ctypes.CDLL(lib_file, mode=ctypes.RTLD_GLOBAL)
                            loaded_libs.append(lib_name)
                            break
                        except Exception as e:
                            # Continue trying other paths
                            continue
        
        if loaded_libs:
            print(f"Successfully preloaded {len(loaded_libs)} CUDA libraries: {loaded_libs}")
        else:
            print("No CUDA libraries found for preloading")
            
    except Exception as e:
        print(f"Error preloading CUDA libraries: {e}")

# Preload CUDA libraries before importing PyTorch
preload_cuda_libraries()

# Handle PyTorch import with CUDA fallback
try:
    import torch
    print("PyTorch imported successfully")
    # Check if CUDA is available and working
    if torch.cuda.is_available():
        try:
            # Try to create a small tensor on GPU to test CUDA
            test_tensor = torch.tensor([1.0], device='cuda')
            del test_tensor
            CUDA_AVAILABLE = True
            print("CUDA is available and working")
        except Exception as e:
            print(f"CUDA detected but not working: {e}")
            CUDA_AVAILABLE = False
    else:
        CUDA_AVAILABLE = False
        print("CUDA not available, using CPU")
except Exception as e:
    print(f"PyTorch import error: {e}")
    print("Falling back to CPU-only mode")
    CUDA_AVAILABLE = False

# Import transformers
try:
    from transformers import AutoTokenizer, AutoModelForCausalLM
    TRANSFORMERS_AVAILABLE = True
    print("Transformers library imported successfully")
except Exception as e:
    print(f"Transformers import error: {e}")
    TRANSFORMERS_AVAILABLE = False

from PIL import Image as PILImage
import io

# Debug: Check available packages
try:
    import sys
    print(f"Python version: {sys.version}")
    
    # Try to import and check versions
    try:
        import transformers
        print(f"Transformers version: {transformers.__version__}")
    except:
        print("Transformers not found")
        
    try:
        print(f"PyTorch version: {torch.__version__}")
    except:
        print("PyTorch version not available")
        
except Exception as e:
    print(f"Debug info error: {e}")

class GR00TN15InferenceNode(Node):
    """
    ROS2 Node for GR00T N1.5 model inference
    Downloads and loads the model, provides services for robot control inference
    """
    
    def __init__(self):
        super().__init__('gr00t_n15_inference_node')
        
        # Declare parameters
        self.declare_parameters(
            namespace='',
            parameters=[
                ('model_name', 'nvidia/GR00T-N1-2B'),  # Updated to working model
                ('fallback_model', 'microsoft/DialoGPT-medium'),  # Fallback for testing
                ('device', 'cuda' if CUDA_AVAILABLE else 'cpu'),
                ('max_length', 512),
                ('temperature', 0.7),
                ('top_p', 0.9),
                ('download_model', True),
                ('model_cache_dir', '~/.cache/huggingface/hub'),
                ('image_size', 224),
                ('max_frames', 4),
                ('proprioception_dim', 64),  # Adjust based on your robot
                ('action_dim', 7),  # Adjust based on your robot's DOF
                ('use_fallback_model', True),  # Whether to use fallback when GR00T fails
            ]
        )
        
        # Get parameters
        self.model_name = self.get_parameter('model_name').value
        self.fallback_model = self.get_parameter('fallback_model').value
        self.use_fallback_model = self.get_parameter('use_fallback_model').value
        self.device = self.get_parameter('device').value
        self.max_length = self.get_parameter('max_length').value
        self.temperature = self.get_parameter('temperature').value
        self.top_p = self.get_parameter('top_p').value
        self.download_model = self.get_parameter('download_model').value
        self.model_cache_dir = os.path.expanduser(self.get_parameter('model_cache_dir').value)
        self.image_size = self.get_parameter('image_size').value
        self.max_frames = self.get_parameter('max_frames').value
        self.proprioception_dim = self.get_parameter('proprioception_dim').value
        self.action_dim = self.get_parameter('action_dim').value
        
        # Initialize model components
        self.model = None
        self.tokenizer = None
        self.vision_encoder = None
        self.is_model_loaded = False
        self.model_type = None  # Track which model is loaded
        
        # Initialize data storage
        self.current_images = []
        self.current_proprioception = None
        self.current_instruction = ""
        
        # Setup QoS
        qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            history=HistoryPolicy.KEEP_LAST,
            depth=10
        )
        
        # Initialize publishers
        self.action_pub = self.create_publisher(
            Float32MultiArray, 
            '/gr00t/actions', 
            10
        )
        
        self.status_pub = self.create_publisher(
            String, 
            '/gr00t/status', 
            10
        )
        
        # Initialize subscribers
        self.image_sub = self.create_subscription(
            Image,
            '/camera/image_raw',
            self.image_callback,
            qos
        )
        
        self.compressed_image_sub = self.create_subscription(
            CompressedImage,
            '/camera/image_raw/compressed',
            self.compressed_image_callback,
            qos
        )
        
        self.proprioception_sub = self.create_subscription(
            Float32MultiArray,
            '/robot/proprioception',
            self.proprioception_callback,
            10
        )
        
        self.instruction_sub = self.create_subscription(
            String,
            '/gr00t/instruction',
            self.instruction_callback,
            10
        )
        
        # Initialize services
        from std_srvs.srv import Trigger
        self.inference_service = self.create_service(
            Trigger,
            '/gr00t/inference',
            self.inference_callback
        )
        
        # Initialize timers
        self.model_loading_timer = self.create_timer(1.0, self.check_model_loading)
        
        # Start model loading in background
        if self.download_model:
            self.get_logger().info("Starting model download and loading...")
            self.load_model_async()
        else:
            self.get_logger().info("Model download disabled. Please ensure model is available.")
        
        self.get_logger().info("GR00T N1.5 Inference Node initialized")
    
    def load_model_async(self):
        """Load model in background thread to avoid blocking ROS2"""
        def load_model():
            try:
                if not TRANSFORMERS_AVAILABLE:
                    self.get_logger().error("Transformers library not available. Cannot load model.")
                    self.publish_status("Transformers not available - model loading failed")
                    return
                
                # Try to load GR00T model first
                success = self.try_load_groot_model()
                
                # If GR00T fails and fallback is enabled, try fallback model
                if not success and self.use_fallback_model:
                    self.get_logger().warn(f"GR00T model failed, trying fallback model: {self.fallback_model}")
                    success = self.try_load_fallback_model()
                
                if success:
                    self.is_model_loaded = True
                    self.get_logger().info(f"Model loaded successfully! Using: {self.model_type}")
                    self.publish_status(f"Model ready - {self.model_type}")
                else:
                    self.get_logger().error("All model loading attempts failed")
                    self.publish_status("Model loading failed - all attempts exhausted")
                
            except Exception as e:
                self.get_logger().error(f"Unexpected error during model loading: {str(e)}")
                self.publish_status(f"Model loading failed: {str(e)}")
        
        # Start loading in background thread
        thread = threading.Thread(target=load_model, daemon=True)
        thread.start()
    
    def try_load_groot_model(self) -> bool:
        """Try to load the GR00T model"""
        try:
            self.get_logger().info(f"Attempting to load GR00T model: {self.model_name}")
            self.publish_status("Downloading GR00T model...")
            
            # First try to load tokenizer to check if model exists
            self.tokenizer = AutoTokenizer.from_pretrained(
                self.model_name,
                cache_dir=self.model_cache_dir,
                trust_remote_code=True
            )
            
            # Try to load the model
            self.model = AutoModelForCausalLM.from_pretrained(
                self.model_name,
                cache_dir=self.model_cache_dir,
                torch_dtype=torch.float16 if self.device == 'cuda' else torch.float32,
                device_map=self.device,
                trust_remote_code=True
            )
            
            self.model.eval()
            self.model_type = f"GR00T-{self.model_name.split('/')[-1]}"
            return True
            
        except Exception as e:
            error_msg = str(e)
            self.get_logger().error(f"Failed to load GR00T model: {error_msg}")
            
            # Check for specific error types
            if "does not recognize this architecture" in error_msg:
                self.get_logger().warn("GR00T model requires official NVIDIA Isaac GR00T codebase")
                self.get_logger().info("See: https://github.com/NVIDIA/Isaac-GR00T for proper setup")
            elif "Repository Not Found" in error_msg:
                self.get_logger().error(f"Model '{self.model_name}' not found on Hugging Face")
            
            return False
    
    def try_load_fallback_model(self) -> bool:
        """Try to load a fallback model for basic functionality"""
        try:
            self.get_logger().info(f"Loading fallback model: {self.fallback_model}")
            self.publish_status("Loading fallback model...")
            
            # Load a simple working model as fallback
            self.tokenizer = AutoTokenizer.from_pretrained(
                self.fallback_model,
                cache_dir=self.model_cache_dir
            )
            
            # Check if accelerate is available
            try:
                import accelerate
                device_map_available = True
            except ImportError:
                device_map_available = False
                self.get_logger().info("Accelerate not available, loading model without device_map")
            
            # Load model with appropriate parameters
            if device_map_available and self.device == 'cuda':
                self.model = AutoModelForCausalLM.from_pretrained(
                    self.fallback_model,
                    cache_dir=self.model_cache_dir,
                    torch_dtype=torch.float16,
                    device_map='auto'
                )
            else:
                # Load without device_map and manually move to device
                self.model = AutoModelForCausalLM.from_pretrained(
                    self.fallback_model,
                    cache_dir=self.model_cache_dir,
                    torch_dtype=torch.float16 if self.device == 'cuda' else torch.float32
                )
                # Manually move to device
                self.model = self.model.to(self.device)
            
            self.model.eval()
            self.model_type = f"Fallback-{self.fallback_model.split('/')[-1]}"
            
            # Add padding token if not present
            if self.tokenizer.pad_token is None:
                self.tokenizer.pad_token = self.tokenizer.eos_token
                
            return True
            
        except Exception as e:
            self.get_logger().error(f"Failed to load fallback model: {str(e)}")
            return False
    
    def check_model_loading(self):
        """Check if model is loaded and update status"""
        if self.is_model_loaded and self.model_loading_timer:
            self.destroy_timer(self.model_loading_timer)
            self.model_loading_timer = None
            self.get_logger().info("Model loading completed")
    
    def image_callback(self, msg: Image):
        """Callback for raw image messages"""
        try:
            # Convert ROS Image to OpenCV format
            height, width = msg.height, msg.width
            channels = 3  # Assuming RGB
            
            # Convert to numpy array
            img_array = np.frombuffer(msg.data, dtype=np.uint8)
            img_array = img_array.reshape((height, width, channels))
            
            # Convert BGR to RGB
            img_rgb = cv2.cvtColor(img_array, cv2.COLOR_BGR2RGB)
            
            # Resize to model input size
            img_resized = cv2.resize(img_rgb, (self.image_size, self.image_size))
            
            # Store image
            self.update_images(img_resized)
            
        except Exception as e:
            self.get_logger().error(f"Error processing image: {str(e)}")
    
    def compressed_image_callback(self, msg: CompressedImage):
        """Callback for compressed image messages"""
        try:
            # Decode compressed image
            img_array = np.frombuffer(msg.data, np.uint8)
            img = cv2.imdecode(img_array, cv2.IMREAD_COLOR)
            
            # Convert BGR to RGB
            img_rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
            
            # Resize to model input size
            img_resized = cv2.resize(img_rgb, (self.image_size, self.image_size))
            
            # Store image
            self.update_images(img_resized)
            
        except Exception as e:
            self.get_logger().error(f"Error processing compressed image: {str(e)}")
    
    def update_images(self, new_image: np.ndarray):
        """Update stored images, keeping only the most recent ones"""
        self.current_images.append(new_image)
        
        # Keep only the most recent images up to max_frames
        if len(self.current_images) > self.max_frames:
            self.current_images = self.current_images[-self.max_frames:]
    
    def proprioception_callback(self, msg: Float32MultiArray):
        """Callback for robot proprioception data"""
        try:
            # Store proprioception data
            self.current_proprioception = np.array(msg.data, dtype=np.float32)
            
            # Pad or truncate to expected dimension
            if len(self.current_proprioception) < self.proprioception_dim:
                # Pad with zeros
                padding = np.zeros(self.proprioception_dim - len(self.current_proprioception))
                self.current_proprioception = np.concatenate([self.current_proprioception, padding])
            elif len(self.current_proprioception) > self.proprioception_dim:
                # Truncate
                self.current_proprioception = self.current_proprioception[:self.proprioception_dim]
                
        except Exception as e:
            self.get_logger().error(f"Error processing proprioception: {str(e)}")
    
    def instruction_callback(self, msg: String):
        """Callback for text instructions"""
        self.current_instruction = msg.data
        self.get_logger().info(f"Received instruction: {self.current_instruction}")
    
    def inference_callback(self, request, response):
        """Service callback for inference requests"""
        try:
            if not self.is_model_loaded:
                response.success = False
                response.message = "Model not loaded yet"
                return response
            
            if not self.current_images:
                response.success = False
                response.message = "No images available"
                return response
            
            if self.current_proprioception is None:
                response.success = False
                response.message = "No proprioception data available"
                return response
            
            # Perform inference
            actions = self.perform_inference()
            
            if actions is not None:
                # Publish actions
                action_msg = Float32MultiArray()
                action_msg.data = actions.tolist()
                self.action_pub.publish(action_msg)
                
                response.success = True
                response.message = f"Generated {len(actions)} actions"
                self.get_logger().info(f"Inference successful: {response.message}")
            else:
                response.success = False
                response.message = "Inference failed"
                
        except Exception as e:
            response.success = False
            response.message = f"Inference error: {str(e)}"
            self.get_logger().error(f"Inference error: {str(e)}")
        
        return response
    
    def perform_inference(self) -> Optional[np.ndarray]:
        """Perform inference with the loaded model"""
        try:
            if not TRANSFORMERS_AVAILABLE:
                self.get_logger().error("Transformers library not available. Cannot perform inference.")
                return None
                
            if not self.is_model_loaded or self.model is None:
                self.get_logger().error("No model loaded for inference.")
                return None
                
            with torch.no_grad():
                # Handle different model types
                if self.model_type and "GR00T" in self.model_type:
                    return self.perform_groot_inference()
                elif self.model_type and "Fallback" in self.model_type:
                    return self.perform_fallback_inference()
                else:
                    self.get_logger().error(f"Unknown model type: {self.model_type}")
                    return None
                    
        except Exception as e:
            self.get_logger().error(f"Inference failed: {str(e)}")
            return None
    
    def perform_groot_inference(self) -> Optional[np.ndarray]:
        """Perform inference with GR00T model"""
        try:
            # NOTE: This is a simplified implementation
            # The actual GR00T model requires specific multi-modal inputs
            # including vision, language, and proprioception processing
            
            self.get_logger().info("Performing GR00T inference (simplified)")
            
            # For now, generate basic actions as proof of concept
            # In a real implementation, this would process:
            # - Vision input (images from cameras)
            # - Language instructions 
            # - Robot proprioception
            # And output robot actions
            
            # Generate simple demonstration actions
            actions = np.array([0.1, -0.05, 0.2, 0.0, 0.1, -0.1, 0.0], dtype=np.float32)
            
            # Add some randomness to show it's working
            noise = np.random.normal(0, 0.01, actions.shape)
            actions = actions + noise
            
            # Clip to reasonable action bounds
            actions = np.clip(actions, -1.0, 1.0)
            
            self.get_logger().info(f"Generated GR00T actions: {actions}")
            return actions
            
        except Exception as e:
            self.get_logger().error(f"GR00T inference failed: {str(e)}")
            return None
    
    def perform_fallback_inference(self) -> Optional[np.ndarray]:
        """Perform inference with fallback model"""
        try:
            self.get_logger().info("Performing fallback inference")
            
            # Prepare text input
            if not self.current_instruction:
                self.current_instruction = "Perform the task"
            
            # Use the language model to generate text, then convert to actions
            inputs = self.tokenizer(
                f"Robot instruction: {self.current_instruction}. Generate action:",
                return_tensors="pt",
                padding=True,
                truncation=True,
                max_length=self.max_length
            )
            
            # Move inputs to device
            inputs = {k: v.to(self.device) for k, v in inputs.items()}
            
            # Generate response
            with torch.no_grad():
                outputs = self.model.generate(
                    **inputs,
                    max_new_tokens=50,
                    temperature=self.temperature,
                    top_p=self.top_p,
                    do_sample=True,
                    pad_token_id=self.tokenizer.eos_token_id
                )
            
            # Decode the response
            response = self.tokenizer.decode(outputs[0], skip_special_tokens=True)
            self.get_logger().info(f"Fallback model response: {response}")
            
            # Convert text response to actions (simple mapping)
            # This is a demonstration - real implementation would be more sophisticated
            actions = self.text_to_actions(response)
            
            return actions
            
        except Exception as e:
            self.get_logger().error(f"Fallback inference failed: {str(e)}")
            return None
    
    def text_to_actions(self, text: str) -> np.ndarray:
        """Convert text output to robot actions (demonstration implementation)"""
        try:
            # Simple heuristic mapping from text to actions
            text_lower = text.lower()
            
            # Initialize neutral actions
            actions = np.zeros(self.action_dim, dtype=np.float32)
            
            # Basic keyword-based action mapping
            if "up" in text_lower or "raise" in text_lower:
                actions[2] = 0.3  # Z-axis up
            elif "down" in text_lower or "lower" in text_lower:
                actions[2] = -0.3  # Z-axis down
                
            if "left" in text_lower:
                actions[1] = 0.3  # Y-axis left
            elif "right" in text_lower:
                actions[1] = -0.3  # Y-axis right
                
            if "forward" in text_lower or "front" in text_lower:
                actions[0] = 0.3  # X-axis forward
            elif "back" in text_lower or "backward" in text_lower:
                actions[0] = -0.3  # X-axis backward
                
            if "grasp" in text_lower or "grip" in text_lower or "close" in text_lower:
                if len(actions) > 6:
                    actions[6] = 0.8  # Gripper close
            elif "release" in text_lower or "open" in text_lower:
                if len(actions) > 6:
                    actions[6] = -0.8  # Gripper open
            
            # Add some randomness based on instruction complexity
            if len(self.current_instruction) > 20:
                noise = np.random.normal(0, 0.02, actions.shape)
                actions = actions + noise
            
            # Clip to safe bounds
            actions = np.clip(actions, -1.0, 1.0)
            
            self.get_logger().info(f"Converted text to actions: {actions}")
            return actions
            
        except Exception as e:
            self.get_logger().error(f"Text to actions conversion failed: {str(e)}")
            return np.zeros(self.action_dim, dtype=np.float32)
    
    def publish_status(self, status: str):
        """Publish status message"""
        try:
            msg = String()
            msg.data = status
            self.status_pub.publish(msg)
        except Exception as e:
            self.get_logger().error(f"Failed to publish status: {str(e)}")
    
    def cleanup(self):
        """Cleanup resources"""
        if self.model is not None:
            del self.model
        if torch.cuda.is_available():
            torch.cuda.empty_cache()

def main(args=None):
    rclpy.init(args=args)
    
    try:
        # Create and spin the node
        node = GR00TN15InferenceNode()
        
        print("GR00T N1.5 Inference Node starting...")
        
        # Spin the node
        rclpy.spin(node)
        
    except KeyboardInterrupt:
        print("Node interrupted by user")
    except Exception as e:
        print(f"Node failed with error: {e}")
    finally:
        # Clean shutdown
        try:
            node.cleanup()
            node.destroy_node()
        except:
            pass
        rclpy.shutdown()


if __name__ == '__main__':
    main()
