import glog
import os
import threading
from typing import Any, Callable, List, Optional

import numpy as np
import torch

# Protobuf generated modules
from ai.proto.ai_model_pb2 import SingleModel
from lerobot.policies.smolvla.configuration_smolvla import SmolVLAConfig
from lerobot.policies.smolvla.modeling_smolvla import SmolVLAPolicy
from lerobot.policies.smolvla.processor_smolvla import make_smolvla_pre_post_processors
from PIL import Image
from sensor_msgs.msg import Image as ImageMsg
from std_msgs.msg import Float32

from ai.models.model_base import ModelBase
from ros2.image_converter import ImageConverter


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

    # TODO(ulee): make this configurable.
    TASK_DESCRIPTION = (
        "Pick up the white object and place it in the center of the table."
    )

    def __init__(self, config: SingleModel):
        super().__init__(config)

        # Additional validation (after _setup_ros2_pub_sub has been called)
        if self._num_subscriptions < 1:
            raise ValueError(
                "SmolVLA inference node requires at least one subscription."
            )

        # Persistent storage for latest observations
        self._latest_image = None
        self._state_history: List[List[float]] = []
        self._mutex = threading.Lock()

        self.model = None
        self.preprocessor = None
        self.postprocessor = None
        self.device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        self.bridge = ImageConverter()
        self.task_description = self.TASK_DESCRIPTION

        # Validate task description
        if not self.task_description:
            raise ValueError("SmolVLA inference node requires a task description.")

        self._initialize_inference()

    def _validate_config(self) -> None:
        """
        Validate the model configuration.

        Note: This is called from ModelBase.__init__ BEFORE _setup_ros2_pub_sub(),
        so we can only access _single_model_config and _model_config here.
        """
        # "SmolVLA inference node requires a model path."
        if (
            not self._single_model_config.pretrained_model_hf_path
            or not self._single_model_config.pretrained_model_local_path
        ):
            raise ValueError("SmolVLA inference node requires a pretrained model path.")

    def _initialize_inference(self) -> None:
        """
        Initialize SmolVLA model and processors.

        This includes:
        1. Loading the pretrained SmolVLA model
        2. Setting up pre/post-processors for data normalization and tokenization
        """
        try:
            # TODO(hmoon): Use the model name from the config.
            model_name = self._single_model_config.smolvla_config.model_name
            glog.info(f"Loading SmolVLA model: {model_name}")
            
            # Load model
            local_model_path = os.path.expanduser(
                self._single_model_config.pretrained_model_local_path
            )
            hf_model_path = os.path.expanduser(
                self._single_model_config.pretrained_model_hf_path
            )
            if os.path.isdir(local_model_path):
                glog.info(
                    f"Loading SmolVLA from local path: {local_model_path}"
                )
                self.model = SmolVLAPolicy.from_pretrained(local_model_path)
            else:
                glog.info(
                    f"Loading SmolVLA from hf path: {hf_model_path}"
                )
                self.model = SmolVLAPolicy.from_pretrained(hf_model_path)

            # CRITICAL: Move ALL model components to device
            # This ensures tensors are on the correct device
            self.model = self.model.to(self.device)

            # Update device in the loaded config
            self.model.config.device = str(self.device)

            # Create preprocessors/postprocessors from the model's config
            self.preprocessor, self.postprocessor = make_smolvla_pre_post_processors(
                config=self.model.config
            )

            # Set to eval mode for inference
            self.model.eval()

            glog.info(
                f"SmolVLA inference initialized successfully on {self.device}. Task: '{self.task_description}'"
            )

        except Exception as e:
            glog.error(f"Failed to initialize SmolVLA: {e}")
            import traceback

            glog.error(traceback.format_exc())
            raise

    def handle_input(
        self,
        subscription_index: int,
        data: Any,
        publish_callback: Callable[[int, Any], None],
    ) -> None:
        """
        Handle input data for SmolVLA inference.
        """
        processed_data = self.preprocess_input(subscription_index, data)

        with self._mutex:
            should_infer = False

            if processed_data["type"] == "image":
                self._latest_image = processed_data["data"]
                should_infer = True
            elif processed_data["type"] == "state":
                self._state_history.append(processed_data["data"])
                # Keep last 10 states
                if len(self._state_history) > 10:
                    self._state_history.pop(0)

            # Run inference if we have a new image (camera rate)
            if should_infer and self._latest_image is not None:
                # We pass the collected state history
                outputs = self.inference(self._latest_image, list(self._state_history))

                if outputs is not None:
                    final_outputs = self.postprocess_output(outputs)
                    for publisher_index in range(self._num_publishers):
                        publish_callback(
                            publisher_index, final_outputs[publisher_index]
                        )

    def preprocess_input(self, subscription_index: int, data: Any) -> Any:
        """
        Preprocess a single input message into SmolVLA-ready format.

        This method receives ONE message at a time (e.g., one Image).
        For images: converts to CHW format with batch dimension.
        """
        if isinstance(data, ImageMsg):
            # Convert ROS Image to numpy array (HWC format, RGB)
            cv_image = self.bridge.imgmsg_to_cv2(data, desired_encoding="rgb8")

            # Normalize the image to [0, 1]
            cv_image = cv_image.astype(np.float32) / 255.0

            # Convert to CHW format (SmolVLA expects C, H, W)
            cv_image_chw = np.transpose(cv_image, (2, 0, 1))

            # Add batch dimension: (1, C, H, W)
            cv_image_batch = np.expand_dims(cv_image_chw, axis=0)

            return {"type": "image", "data": cv_image_batch}
        elif isinstance(data, Float32):
            # Return the float value for joint state
            return {"type": "state", "data": data.data}
        else:
            glog.warning(f"Unexpected input type {type(data)}, returning as-is")
            return {"type": "unknown", "data": data}

    def postprocess_output(self, output_data: List[Any]) -> List[Any]:
        """
        Postprocess output data for SmolVLA inference.

        SmolVLA select_action returns actions that may already be normalized or
        in an unknown range. We clamp to [-1, 1] since the actuator driver
        expects ENCODER_DATA_MODE_NORMALIZED_MINUS_ONE_TO_ONE.
        """
        # Handle None or failed inference
        if output_data is None:
            glog.warning("Inference returned None, returning zeros")
            raise ValueError("Inference returned None")

        # select_action returns the action tensor directly
        action_tensor = output_data
        # TODO(ulee): let's revisit normalizing the output to the actuator driver range.
        # unnormalized = self.postprocessor({"action": action_tensor})
        # action_tensor = unnormalized["action"]

        # Shape is typically (batch_size, action_dim) or (batch_size, chunk_size, action_dim)
        # Take the first timestep if chunk_size > 1
        if action_tensor.dim() == 3:
            action_tensor = action_tensor[0, 0]  # Remove batch and take first timestep
        elif action_tensor.dim() == 2:
            action_tensor = action_tensor[0]  # Just remove batch dimension

        action_values = action_tensor.detach().cpu().numpy().tolist()

        # Log raw values for debugging
        glog.debug(f"Raw model output: {action_values}")

        # SmolVLA output is likely already normalized to roughly [-1, 1]
        # Just clamp to ensure values stay in [-1, 1] for actuator driver
        action_values = [max(-1.0, min(1.0, v)) for v in action_values]

        glog.debug(f"Clamped output: {action_values}")

        if len(action_values) != self._num_publishers:
            glog.warning(
                f"Model output {len(action_values)} actions, but need {self._num_publishers}. Padding/truncating."
            )
            # Pad with zeros or truncate
            if len(action_values) < self._num_publishers:
                action_values.extend(
                    [0.0] * (self._num_publishers - len(action_values))
                )
            else:
                action_values = action_values[: self._num_publishers]

        return action_values

    def inference(self, image: Any, state_history: List[Any]) -> List[Any]:
        """
        Run inference using the latest image and state history.

        Args:
            image: Preprocessed image batch (1, C, H, W)
            state_history: List of recent state values

        Returns:
            Action tensor from the model
        """
        # Build joint state from collected state data
        if len(state_history) > 0:
            # Create array from history.
            # Note: Depending on model expectation, we might need a specific number of states.
            # Here we take up to last 6.
            state_vals = state_history[-6:]
            # Ensure it is a flat list of floats if state_history is simple list
            # But if state_history contains lists (e.g. from multiple joints), we need to flatten
            # Here we assume state_history is just simple floats for now based on Float32 input

            state = np.array(state_vals, dtype=np.float32).reshape(1, -1)

            # If we have fewer than 6 states, pad with zeros
            if state.shape[1] < 6:
                state = np.pad(
                    state, ((0, 0), (0, 6 - state.shape[1])), mode="constant"
                )
        else:
            state = np.zeros((1, 6), dtype=np.float32)

        # Prepare observation dictionary for SmolVLA
        # Order: Camera, State, Task
        observation = {
            "observation.images.camera1": image,
            "observation.state": state,
        }

        # Add task description
        task = self.task_description
        if not task.endswith("\n"):
            task = f"{task}\n"

        batch = {
            **observation,
            "task": task,
        }

        try:
            # Preprocess the batch for the model (tokenization, normalization, etc.)
            processed_batch = self.preprocessor(batch)

            # Move all tensors in the batch to the correct device (GPU if available)
            for key in processed_batch:
                if isinstance(processed_batch[key], torch.Tensor):
                    processed_batch[key] = processed_batch[key].to(self.device)

            # Run inference
            with torch.inference_mode():
                outputs = self.model.select_action(processed_batch)

            return outputs

        except Exception as e:
            glog.error(f"Inference failed: {e}")
            import traceback

            glog.error(traceback.format_exc())
            return None

    def forward(self, input_data: List[Any]) -> List[Any]:
        """
        Training forward pass. Not implemented for SmolVLA.
        """
        return []
