"""SmolVLA vision-language-action inference adapter.

Wraps HuggingFace's SmolVLA policy (via lerobot) behind the
``InferenceAdapter`` contract. Consumes decoded camera images and scalar
joint states, runs the policy, and emits normalized joint actions.

References:
- https://github.com/huggingface/lerobot/tree/main/src/lerobot/policies/smolvla
- Pretrained model: lerobot/smolvla_base
"""

from __future__ import annotations

import os
import threading
from typing import Any, Dict, List, Optional

import glog
import numpy as np
import torch
from ai.proto.ai_model_pb2 import SingleModel
from lerobot.policies.smolvla.modeling_smolvla import SmolVLAPolicy
from lerobot.policies.smolvla.processor_smolvla import make_smolvla_pre_post_processors

from ai.runtime.adapter import InferenceAdapter
from ai.runtime.types import (
    ActionCommand,
    AdapterSpec,
    ChannelRole,
    Observation,
    TriggerMode,
)
from config.proto import config_pb2
from ros2.proto import ros2_data_type_pb2


class SmolVlaAdapter(InferenceAdapter):
    """SmolVLA policy adapter for multi-camera manipulation."""

    # TODO(ulee): make this configurable.
    TASK_DESCRIPTION = (
        "Pick up the white object and place it in the center of the table."
    )

    STATE_HISTORY_LEN = 10
    STATE_DIM = 6

    def __init__(self, single_model: SingleModel, config: config_pb2.Config):
        super().__init__(single_model, config)
        self._model_config = single_model.smolvla_config

        self._latest_images: Dict[str, Any] = {}
        self._state_history: List[float] = []
        self._mutex = threading.Lock()
        self._camera_keys_by_subscription: Dict[int, str] = {}
        self._expected_num_cameras = 0

        self.model = None
        self.preprocessor = None
        self.postprocessor = None
        self.device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        self.task_description = self.TASK_DESCRIPTION

    def spec(self) -> AdapterSpec:
        return AdapterSpec(
            trigger_mode=TriggerMode.EVENT,
            min_subscriptions=1,
            min_publishers=1,
        )

    def validate(self) -> None:
        if not (
            self._single_model.pretrained_model_hf_path
            or self._single_model.pretrained_model_local_path
        ):
            raise ValueError("SmolVLA adapter requires a pretrained model path.")
        if not self.task_description:
            raise ValueError("SmolVLA adapter requires a task description.")

    def initialize(self) -> None:
        self._initialize_camera_keys()
        self._initialize_inference()

    def _initialize_camera_keys(self) -> None:
        """Map IMAGE subscription indices to SmolVLA camera keys in order."""
        image_subscription_indices = [
            idx
            for idx, sub in enumerate(self._single_model.node.subscriptions)
            if sub.ros2_data_type == ros2_data_type_pb2.Ros2DataType.IMAGE
        ]
        self._expected_num_cameras = len(image_subscription_indices)
        self._camera_keys_by_subscription = {
            sub_idx: f"observation.images.camera{cam_idx}"
            for cam_idx, sub_idx in enumerate(image_subscription_indices, start=1)
        }

    def _initialize_inference(self) -> None:
        try:
            model_name = self._model_config.model_name
            glog.info(f"Loading SmolVLA model: {model_name}")

            local_model_path = os.path.expanduser(
                self._single_model.pretrained_model_local_path
            )
            hf_model_path = os.path.expanduser(
                self._single_model.pretrained_model_hf_path
            )
            if os.path.isdir(local_model_path):
                glog.info(f"Loading SmolVLA from local path: {local_model_path}")
                self.model = SmolVLAPolicy.from_pretrained(local_model_path)
            else:
                glog.info(f"Loading SmolVLA from hf path: {hf_model_path}")
                self.model = SmolVLAPolicy.from_pretrained(hf_model_path)

            self.model = self.model.to(self.device)
            self.model.config.device = str(self.device)

            self.preprocessor, self.postprocessor = make_smolvla_pre_post_processors(
                config=self.model.config
            )
            self.model.eval()

            glog.info(
                f"SmolVLA initialized on {self.device}. "
                f"Task: '{self.task_description}'"
            )
        except Exception as e:
            glog.error(f"Failed to initialize SmolVLA: {e}")
            import traceback

            glog.error(traceback.format_exc())
            raise

    def on_observation(self, observation: Observation) -> Optional[List[ActionCommand]]:
        with self._mutex:
            if observation.role == ChannelRole.IMAGE:
                camera_key = self._camera_keys_by_subscription.get(
                    observation.channel_index,
                    f"observation.images.camera{observation.channel_index + 1}",
                )
                self._latest_images[camera_key] = self._prepare_image(
                    observation.payload
                )
            elif observation.role == ChannelRole.SCALAR:
                self._state_history.append(float(observation.payload))
                if len(self._state_history) > self.STATE_HISTORY_LEN:
                    self._state_history.pop(0)
                return None
            else:
                glog.warning(f"Ignoring observation role {observation.role}")
                return None

            # Inference is driven by camera frames; wait for all cameras.
            if not self._latest_images:
                return None
            if (
                self._expected_num_cameras > 0
                and len(self._latest_images) < self._expected_num_cameras
            ):
                glog.debug(
                    "Waiting for all camera images (%d/%d).",
                    len(self._latest_images),
                    self._expected_num_cameras,
                )
                return None

            images = dict(self._latest_images)
            state_history = list(self._state_history)

        outputs = self._run_inference(images, state_history)
        if outputs is None:
            return None
        return self._postprocess(outputs)

    def _prepare_image(self, image_hwc_rgb8: np.ndarray) -> np.ndarray:
        """HWC rgb8 uint8 -> (1, C, H, W) float32 in [0, 1]."""
        normalized = image_hwc_rgb8.astype(np.float32) / 255.0
        chw = np.transpose(normalized, (2, 0, 1))
        return np.expand_dims(chw, axis=0)

    def _run_inference(
        self, images: Dict[str, Any], state_history: List[float]
    ) -> Optional[Any]:
        if len(state_history) > 0:
            state_vals = state_history[-self.STATE_DIM :]
            state = np.array(state_vals, dtype=np.float32).reshape(1, -1)
            if state.shape[1] < self.STATE_DIM:
                state = np.pad(
                    state,
                    ((0, 0), (0, self.STATE_DIM - state.shape[1])),
                    mode="constant",
                )
        else:
            state = np.zeros((1, self.STATE_DIM), dtype=np.float32)

        task = self.task_description
        if not task.endswith("\n"):
            task = f"{task}\n"

        batch = {**images, "observation.state": state, "task": task}

        try:
            processed_batch = self.preprocessor(batch)
            for key in processed_batch:
                if isinstance(processed_batch[key], torch.Tensor):
                    processed_batch[key] = processed_batch[key].to(self.device)

            with torch.inference_mode():
                return self.model.select_action(processed_batch)
        except Exception as e:
            glog.error(f"Inference failed: {e}")
            import traceback

            glog.error(traceback.format_exc())
            return None

    def _postprocess(self, action_tensor: Any) -> List[ActionCommand]:
        """Model output tensor -> normalized [-1, 1] ActionCommands."""
        if action_tensor.dim() == 3:
            action_tensor = action_tensor[0, 0]
        elif action_tensor.dim() == 2:
            action_tensor = action_tensor[0]

        action_values = action_tensor.detach().cpu().numpy().tolist()
        glog.debug(f"Raw model output: {action_values}")

        action_values = [max(-1.0, min(1.0, v)) for v in action_values]

        if len(action_values) != self._num_publishers:
            glog.warning(
                f"Model output {len(action_values)} actions but need "
                f"{self._num_publishers}. Padding/truncating."
            )
            if len(action_values) < self._num_publishers:
                action_values.extend(
                    [0.0] * (self._num_publishers - len(action_values))
                )
            else:
                action_values = action_values[: self._num_publishers]

        glog.debug(f"Normalized output: {action_values}")
        return [
            ActionCommand(
                publisher_index=publisher_index,
                value=float(value),
                normalized=True,
            )
            for publisher_index, value in enumerate(action_values)
        ]
