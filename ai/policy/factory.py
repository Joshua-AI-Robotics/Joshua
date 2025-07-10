import logging as glog
from typing import Union

from transformers import PreTrainedModel, PretrainedConfig

from ai.policy.decision_transformer.config_decision_transformer import (
    DecisionTransformerConfig,
)
from ai.policy.decision_transformer.modeling_decision_transformer import (
    DecisionTransformerPolicy,
)

# A type hint for any valid policy configuration.
PolicyConfig = Union[PretrainedConfig]

CONFIG_MAPPING = {"decision_transformer": DecisionTransformerConfig}
POLICY_MAPPING = {"decision_transformer": DecisionTransformerPolicy}


def create_policy(policy_config: PolicyConfig) -> PreTrainedModel:
    """Creates a policy instance from a policy config. The model *is* the policy."""
    model_class = POLICY_MAPPING.get(policy_config.model_type)
    if model_class:
        return model_class(policy_config)
    else:
        raise ValueError(f"Invalid policy model type: {policy_config.model_type}")


def create_policy_config(policy_name: str, **kwargs) -> PolicyConfig:
    """
    Creates a policy configuration object from a policy name.
    Any extra kwargs are passed to the config's constructor.
    """
    config_class = CONFIG_MAPPING.get(policy_name)
    if config_class:
        return config_class(**kwargs)
    else:
        raise ValueError(f"Invalid policy name: {policy_name}")
