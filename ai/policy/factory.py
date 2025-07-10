import logging as glog
from typing import Union

from transformers import PreTrainedModel

from ai.policy.decision_transformer.config_decision_transformer import (
    DecisionTransformerConfig,
)
from ai.policy.decision_transformer.modeling_decision_transformer import (
    DecisionTransformer,
)

# A type hint for any valid policy configuration.
PolicyConfig = Union[DecisionTransformerConfig]


def create_policy(policy_config: PolicyConfig) -> PreTrainedModel:
    """Creates a policy instance from a policy config. The model *is* the policy."""
    if isinstance(policy_config, DecisionTransformerConfig):
        return DecisionTransformer(policy_config)
    else:
        config_class_name = policy_config.__class__.__name__
        raise ValueError(f"Invalid policy config type: {config_class_name}")


def create_policy_config(policy_name: str, **kwargs) -> PolicyConfig:
    """
    Creates a policy configuration object from a policy name.
    Any extra kwargs are passed to the config's constructor.
    """
    if policy_name == "decision_transformer":
        return DecisionTransformerConfig(**kwargs)
    else:
        raise ValueError(f"Invalid policy name: {policy_name}")
