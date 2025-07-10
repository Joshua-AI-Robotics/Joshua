import torch
import torch.nn as nn
from collections import deque

from transformers import PreTrainedModel, DecisionTransformerModel as HFDecisionTransformerModel
from .config_decision_transformer import DecisionTransformerConfig

# This will be available at runtime because of the bazel dependencies.
from robot.nexus.proto import nexus_packet_pb2


class VisionEncoder(nn.Module):
    """
    A simple CNN to encode image observations into a feature vector.
    """
    def __init__(self, embedding_dim=128, image_size=(128, 128)):
        super().__init__()
        self.cnn = nn.Sequential(
            nn.Conv2d(3, 32, kernel_size=8, stride=4, padding=0),
            nn.ReLU(),
            nn.Conv2d(32, 64, kernel_size=4, stride=2, padding=0),
            nn.ReLU(),
            nn.Conv2d(64, 64, kernel_size=3, stride=1, padding=0),
            nn.ReLU(),
            nn.Flatten(),
        )

        # Dummy forward pass to compute the output dimension of the CNN
        with torch.no_grad():
            dummy_input = torch.zeros(1, 3, image_size[0], image_size[1])
            cnn_out_dim = self.cnn(dummy_input).shape[1]

        self.fc = nn.Linear(cnn_out_dim, embedding_dim)
        self.ln = nn.LayerNorm(embedding_dim)

    def forward(self, x):
        # Normalize images from [0, 255] to [0, 1]
        x = x.float() / 255.0
        x = self.cnn(x)
        x = self.fc(x)
        x = self.ln(x)
        return x

class MultiModalDecisionTransformer(PreTrainedModel):
    """
    A Decision Transformer model that processes both image and low-dimensional state inputs.
    This class contains the full policy logic, including history management for inference.
    """
    config_class = DecisionTransformerConfig

    def __init__(self, config: DecisionTransformerConfig):
        super().__init__(config)
        self.config = config
        
        self.vision_encoder = VisionEncoder(
            embedding_dim=config.embedding_dim,
            image_size=(config.image_size_h, config.image_size_w)
        )
        self.motor_encoder_dim = config.motor_encoder_dim

        self.dt = HFDecisionTransformerModel(config)

        # --- Inference History Buffers ---
        self.context_length = config.max_length
        self.action_dim = config.act_dim
        self.image_history = deque(maxlen=self.context_length)
        self.motor_history = deque(maxlen=self.context_length)
        self.action_history = deque(maxlen=self.context_length)
        self.rtg_history = deque(maxlen=self.context_length)
        self.timestep_history = deque(maxlen=self.context_length)
        self.timestep = 0

    def forward(self, image_states, motor_states, actions, returns_to_go, timesteps, attention_mask=None):
        batch_size, seq_len = image_states.shape[0], image_states.shape[1]

        # Embed images
        image_embeddings = self.vision_encoder(image_states.view(-1, 3, self.config.image_size_h, self.config.image_size_w))
        image_embeddings = image_embeddings.view(batch_size, seq_len, -1)

        # Combine with motor states
        state_embeddings = torch.cat([image_embeddings, motor_states], dim=-1)

        # Pass to the underlying transformer
        return self.dt(
            states=state_embeddings,
            actions=actions,
            returns_to_go=returns_to_go,
            timesteps=timesteps,
            attention_mask=attention_mask,
        )

    @torch.no_grad()
    def get_action(self, nexus_input: nexus_packet_pb2.NexusModelInputPacket, reward: float):
        
        # --- 1. Update History Buffers ---
        if self.timestep == 0: # Reset for new episode
            self.image_history.clear()
            self.motor_history.clear()
            self.action_history.clear()
            self.rtg_history.clear()
            self.timestep_history.clear()

        # TODO: Implement robust logic to parse image and motor data from perception_packets
        current_image_state = torch.zeros(1, 3, self.config.image_size_h, self.config.image_size_w)
        current_motor_state = torch.zeros(1, self.motor_encoder_dim)

        current_rtg = self.rtg_history[-1] - reward if len(self.rtg_history) > 0 else nexus_input.target_return_to_go

        self.image_history.append(current_image_state)
        self.motor_history.append(current_motor_state)
        self.rtg_history.append(current_rtg)
        self.timestep_history.append(self.timestep)
        self.action_history.append(torch.zeros(1, self.action_dim)) # Placeholder for current action

        # --- 2. Assemble Model Inputs ---
        pad_len = self.context_length - len(self.image_history)
        
        images = torch.cat([torch.zeros(pad_len, 3, self.config.image_size_h, self.config.image_size_w)] + list(self.image_history), dim=0).unsqueeze(0)
        motors = torch.cat([torch.zeros(pad_len, self.motor_encoder_dim)] + list(self.motor_history), dim=0).unsqueeze(0)
        actions = torch.cat([torch.zeros(pad_len, self.action_dim)] + list(self.action_history), dim=0).unsqueeze(0)
        rtgs = torch.tensor(list(self.rtg_history) + [0.0] * pad_len, dtype=torch.float32).unsqueeze(0).unsqueeze(-1)
        timesteps = torch.tensor(list(self.timestep_history) + [0] * pad_len, dtype=torch.long).unsqueeze(0)
        attention_mask = torch.cat([torch.zeros(pad_len, dtype=torch.long), torch.ones(len(self.image_history), dtype=torch.long)], dim=0).unsqueeze(0)

        # --- 3. Get Action from Model ---
        model_output = self(
            image_states=images,
            motor_states=motors,
            actions=actions,
            returns_to_go=rtgs,
            timesteps=timesteps,
            attention_mask=attention_mask,
        )

        action = model_output.action_preds[0, -1]
        
        # --- 4. Update History and Timestep ---
        self.action_history[-1] = action.unsqueeze(0)
        self.timestep += 1

        return action.detach() 