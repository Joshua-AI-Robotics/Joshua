import logging as glog
from ai.policy.base_policy import BasePolicy
from ai.configs.base_policy_config import BasePolicyConfig
from ai.policy.pi0.pi0_policy import Pi0Policy  
from ai.policy.tdmpc.tdmpc_policy import TdmpcPolicy
from ai.policy.pi0.pi0_config import Pi0Config
from ai.policy.tdmpc.tdmpc_config import TdmpcConfig

def create_policy(policy_config: BasePolicyConfig) -> BasePolicy:
    if policy_config.policy_name == "pi0":
        return Pi0Policy()
    elif policy_config.policy_name == "tdmpc":
        return TdmpcPolicy()
    else:
        raise ValueError(f"Invalid policy name: {policy_config.policy_name}")

def create_policy_config(policy_name: str) -> BasePolicyConfig:
    if policy_name == "pi0":
        return Pi0Config()
    elif policy_name == "tdmpc":
        return TdmpcConfig()
    else:
        raise ValueError(f"Invalid policy name: {policy_name}")
