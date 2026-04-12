"""Shared helpers for MuJoCo Ant PPO training/playback."""

from __future__ import annotations

import torch
from skrl.models.torch import DeterministicMixin, GaussianMixin, Model


def make_mlp(
    input_dim: int, hidden_dims: list[int], output_dim: int
) -> torch.nn.Sequential:
    layers: list[torch.nn.Module] = []
    in_dim = input_dim
    for hidden in hidden_dims:
        layers.extend((torch.nn.Linear(in_dim, hidden), torch.nn.ELU()))
        in_dim = hidden
    layers.append(torch.nn.Linear(in_dim, output_dim))
    return torch.nn.Sequential(*layers)


class Policy(GaussianMixin, Model):
    def __init__(
        self,
        observation_space,
        action_space,
        device,
        obs_size: int,
        action_size: int,
        hidden_dims: list[int],
    ):
        Model.__init__(self, observation_space, action_space, device)
        GaussianMixin.__init__(
            self,
            clip_actions=True,
            min_log_std=-20.0,
            max_log_std=2.0,
        )
        self.net = make_mlp(obs_size, hidden_dims, action_size)
        self.log_std_parameter = torch.nn.Parameter(torch.zeros(action_size))

    def compute(self, inputs, role=""):
        return self.net(inputs["states"]), self.log_std_parameter, {}


class Value(DeterministicMixin, Model):
    def __init__(
        self,
        observation_space,
        action_space,
        device,
        obs_size: int,
        hidden_dims: list[int],
    ):
        Model.__init__(self, observation_space, action_space, device)
        DeterministicMixin.__init__(self, clip_actions=False)
        self.net = make_mlp(obs_size, hidden_dims, 1)

    def compute(self, inputs, role=""):
        return self.net(inputs["states"]), {}
