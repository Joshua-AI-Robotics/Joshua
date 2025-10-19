# SmolVLA Inference Node - Usage Guide

## Overview

This guide explains how to use the SmolVLA inference node in Project Joshua. SmolVLA (Small Vision-Language-Action) is a vision-language-action model from HuggingFace LeRobot that can be used for robotic manipulation tasks.

## What is SmolVLA?

SmolVLA is a transformer-based model that:
- Takes camera images as input
- Optionally takes text instructions (e.g., "pick up the red block")
- Outputs robot action commands (joint positions, gripper commands, etc.)
- Is pretrained on robotic manipulation datasets

## Prerequisites

### Software Requirements

All dependencies are already in `requirements.txt`:
- `torch` >= 2.3.0
- `transformers` >= 4.53.2
- `pillow` >= 11.3.0
- `numpy` >= 1.26.4
- `huggingface-hub`
- `accelerate`

### Hardware Requirements

**Recommended:**
- GPU with CUDA support (for real-time inference)
- At least 8GB VRAM (for SmolVLA-1.7B)
- Multiple cameras for better perception

**Minimum:**
- CPU-only mode is supported but will be slower
- At least 16GB RAM

## Quick Start

### 1. Build the Node

```bash
cd /home/unghee/codes/ProjectJoshua
bazel build //ros2:smolvla_inference
```

### 2. Prepare Your Model

#### Option A: Use Pretrained HuggingFace Model

The node will automatically download the model on first run:
```python
model_checkpoint_path: "HuggingFaceH4/SmolVLA-1.7B"
```

This requires an internet connection on the first run. The model will be cached locally for subsequent runs.

#### Option B: Use Local Checkpoint

If you have a fine-tuned model or want to use a local checkpoint:
```python
model_checkpoint_path: "/path/to/your/local/checkpoint"
```

### 3. Configure Your Robot

Edit the configuration file at `config/config_preset/so100_smolvla_inference.pbtxt`:

```protobuf
ai {
  # Camera topics (match your camera publishers)
  subscribe_topics: "/camera/image_0"
  subscribe_topics: "/camera/image_1"  # Add more cameras as needed
  
  # Action topics (match your actuator subscribers)
  publish_topics: "/action/joint_0"
  publish_topics: "/action/joint_1"
  # ... add all your robot's joints
  
  # Optional: Task instruction
  # task_instruction: "Pick up the object and place it in the container"
}
```

### 4. Run the Node

```bash
bazel run //ros2:smolvla_inference -- --config config/config_preset/so100_smolvla_inference.pbtxt
```

## Configuration Options

### Model Settings

The SmolVLA node automatically configures several settings:

**Device Selection:**
- Automatically uses CUDA if available
- Falls back to CPU if no GPU is detected
- You can check the logs to see which device is being used:
  ```
  [smolvla_inference]: Using device: cuda
  ```

**Precision:**
- Uses `bfloat16` on GPU for faster inference
- Uses `float32` on CPU for compatibility

### Camera Setup

The node supports multiple camera inputs. Each camera topic should publish `sensor_msgs/Image` messages.

**Supported Image Encodings:**
- `rgb8`: Standard RGB (8 bits per channel)
- `bgr8`: OpenCV format (automatically converted to RGB)
- `mono8`: Grayscale (converted to RGB by repeating channels)
- `rgba8`: RGB with alpha (alpha channel is dropped)

Example multi-camera setup:
```protobuf
ai {
  subscribe_topics: "/camera/left/image_raw"    # Left wrist camera
  subscribe_topics: "/camera/right/image_raw"   # Right wrist camera
  subscribe_topics: "/camera/top/image_raw"     # Top-down view
}
```

### Action Outputs

The node publishes `std_msgs/Float32` messages to action topics. Actions are automatically:
- Clipped to range [-1.0, 1.0] (configurable)
- Matched to the number of publish topics
- Padded with zeros or truncated if model output doesn't match

## Running a Complete System

Here's how to run a complete inference pipeline:

### Terminal 1: Start Camera Publishers
```bash
bazel run //ros2:camera_publisher -- --config your_config.pbtxt
```

### Terminal 2: Start SmolVLA Inference
```bash
bazel run //ros2:smolvla_inference -- --config config/config_preset/so100_smolvla_inference.pbtxt
```

### Terminal 3: Start Actuator Subscribers
```bash
bazel run //ros2:actuator_subscriber -- --config your_config.pbtxt
```

### Terminal 4: Monitor Topics (Optional)
```bash
# List all topics
ros2 topic list

# Monitor action outputs
ros2 topic echo /action/joint_0

# Check camera feed
ros2 topic hz /camera/image_0

# Check inference rate
ros2 topic hz /action/joint_0
```

## Customization

### Using Task Instructions

SmolVLA supports natural language instructions. To use them:

1. Add to your config:
```protobuf
ai {
  task_instruction: "Pick up the red block and place it on the blue plate"
}
```

2. The instruction will be embedded with the images during inference
3. The model will condition its actions on both the visual input and the text

### Fine-tuning for Your Robot

To fine-tune SmolVLA on your own data:

1. Collect demonstration data using your robot
2. Format data according to LeRobot dataset format
3. Fine-tune using HuggingFace training scripts
4. Point `model_checkpoint_path` to your fine-tuned checkpoint

Resources:
- [LeRobot GitHub](https://github.com/huggingface/lerobot)
- [SmolVLA Documentation](https://huggingface.co/HuggingFaceH4/SmolVLA-1.7B)

### Adjusting Action Bounds

In `smolvla_inference.py`, you can modify action bounds:

```python
# In __init__
self.action_min = -1.0  # Change minimum action value
self.action_max = 1.0   # Change maximum action value
```

### Adding Action Normalization

You can add custom normalization/denormalization:

```python
def infer(self, sensor_data):
    # ... existing code ...
    
    # Denormalize actions from [-1, 1] to your robot's range
    action_list = [
        self._denormalize_action(action, joint_idx)
        for joint_idx, action in enumerate(action_list)
    ]
    
    return action_list

def _denormalize_action(self, normalized_action, joint_idx):
    # Example: different joints have different ranges
    joint_ranges = {
        0: (-180, 180),  # degrees
        1: (-90, 90),
        # ... etc
    }
    min_val, max_val = joint_ranges.get(joint_idx, (-1, 1))
    return normalized_action * (max_val - min_val) / 2 + (max_val + min_val) / 2
```

## Troubleshooting

### "Model Not Found" Error

**Problem:** Cannot load model from HuggingFace

**Solutions:**
1. Check internet connection
2. Verify model path is correct: `"HuggingFaceH4/SmolVLA-1.7B"`
3. Try downloading manually first:
   ```python
   from transformers import AutoModelForVision2Seq
   model = AutoModelForVision2Seq.from_pretrained("HuggingFaceH4/SmolVLA-1.7B")
   ```

### Out of Memory (OOM) Error

**Problem:** GPU runs out of memory

**Solutions:**
1. Use a smaller model variant if available
2. Reduce batch size (currently processes one inference at a time)
3. Use CPU mode (slower but uses system RAM)
4. Close other GPU-intensive applications

### Slow Inference

**Problem:** Inference is too slow for real-time control

**Solutions:**
1. Ensure you're using GPU (check logs for "Using device: cuda")
2. Verify CUDA is properly installed: `nvidia-smi`
3. Consider model optimization:
   - Quantization (int8)
   - TorchScript compilation
   - ONNX export
4. Reduce image resolution in camera publisher

### Actions Not Changing

**Problem:** Robot receives same actions repeatedly

**Solutions:**
1. Check that cameras are publishing different images
2. Verify task instruction is appropriate
3. Model may need fine-tuning on your specific task
4. Check action bounds aren't clipping everything to same value

### "Unsupported encoding" Warning

**Problem:** Camera image encoding not recognized

**Solutions:**
1. Check camera encoding: `ros2 topic echo /camera/image_0 --once`
2. Add support for your encoding in `_ros_image_to_pil()` method
3. Reconfigure camera to use supported encoding (rgb8, bgr8, etc.)

### Actions are Zeros

**Problem:** Model always outputs zero actions

**Solutions:**
1. Model may not be properly loaded - check logs for errors
2. Model might not be trained for your task
3. Input images might not be in expected format
4. Try with task instruction if you weren't using one

## Performance Tips

### Optimize Inference Speed

1. **Use GPU:** 10-50x faster than CPU
2. **Reduce Camera Resolution:** Lower resolution = faster processing
3. **Use Efficient Encodings:** rgb8 is faster than conversions
4. **Monitor Inference Rate:** 
   ```bash
   ros2 topic hz /action/joint_0
   ```
   Target: 10-30 Hz for responsive control

### Optimize Memory Usage

1. **Use bfloat16:** Already enabled on GPU
2. **Close Unused Applications:** Free up VRAM
3. **Single Camera:** Use fewer cameras if possible
4. **Batch Processing:** Not implemented yet, but could help

## Advanced Topics

### Integrating with Other Models

The `InferenceBase` class makes it easy to swap models. To try a different model:

1. Create a new file (e.g., `pi0_inference.py`)
2. Inherit from `InferenceBase`
3. Implement `_initialize_inference()` and `infer()`
4. Add to BUILD file

### Multi-Modal Inputs

To add non-visual inputs (e.g., joint encoders, force sensors):

1. Modify `InferenceBase` to support multiple message types
2. Update `_setup_subscriptions()` to handle different types
3. Pass all sensor data to `infer()`
4. Concatenate with image features in your model

### Action History

To use temporal information:

1. Store recent actions in a buffer
2. Pass history to model along with current observations
3. Model can learn temporal dependencies

Example:
```python
def __init__(self, ...):
    super().__init__(...)
    self.action_history = []
    self.history_length = 10

def infer(self, sensor_data):
    # ... get actions ...
    
    # Update history
    self.action_history.append(action_list)
    if len(self.action_history) > self.history_length:
        self.action_history.pop(0)
    
    return action_list
```

## Examples

### Example 1: Basic Pick-and-Place

```bash
# Config: so100_smolvla_inference.pbtxt
ai {
  subscribe_topics: "/camera/wrist/image"
  subscribe_topics: "/camera/external/image"
  publish_topics: "/action/joint_0"
  publish_topics: "/action/joint_1"
  publish_topics: "/action/joint_2"
  publish_topics: "/action/gripper"
  task_instruction: "Pick up the red block"
}
```

### Example 2: Multi-Step Task

```bash
# Config with detailed instruction
ai {
  task_instruction: "First, pick up the blue cube. Then, place it in the left container. Finally, pick up the red cylinder and place it in the right container."
}
```

### Example 3: Custom Checkpoint

```bash
# Config with fine-tuned model
ai {
  model_checkpoint_path: "/home/user/models/smolvla-finetuned-so100"
  task_instruction: "Perform the assembly task"
}
```

## Resources

- **SmolVLA Model:** https://huggingface.co/HuggingFaceH4/SmolVLA-1.7B
- **LeRobot GitHub:** https://github.com/huggingface/lerobot
- **Transformers Docs:** https://huggingface.co/docs/transformers
- **Project Joshua Docs:** See main README.md

## Getting Help

If you encounter issues:

1. Check this troubleshooting guide
2. Review ROS2 logs for error messages
3. Verify configuration matches your robot setup
4. Test with mock_inference.py first to verify pipeline works
5. Open an issue on the Project Joshua repository

## Next Steps

After getting SmolVLA working:

1. Collect demonstration data on your robot
2. Fine-tune the model on your specific tasks
3. Optimize inference speed for your application
4. Experiment with different task instructions
5. Try other models (Pi0, custom models, etc.)

