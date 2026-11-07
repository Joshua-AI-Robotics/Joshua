"""CAD trajectory prior with bounded, learned corrections for a four-leg crawl.

Inspired by PMTG and CPG/residual locomotion architectures; no external robot's
geometry, actuator gains, or implementation is reused. See the trainer README.
"""

from dataclasses import dataclass

import torch
from mjlab.envs.mdp.actions import JointPositionAction, JointPositionActionCfg

from .contract import JOINT_NAMES
from .gait import SAMPLES, joint_reference

PARAMETERS = {
    "residual_scale_rad": 0.08,
    "tracking_gain": 2.0,
    "phase_advance_s": 0.04,
    "measured_clearance_m": 0.012,
}


def bounded_residual(nominal, action, lower, upper, scale):
    """Clip the nominal first so accepted residual feedback stays in [-1, 1]."""
    base = nominal.clamp(min=lower, max=upper)
    proposed = base + scale * action.clamp(-1, 1)
    target = proposed.clamp(min=lower, max=upper)
    accepted = (target - base) / scale
    saturated = (nominal != base) | (proposed != target)
    return target, accepted, saturated


class CrawlResidualAction(JointPositionAction):
    """Apply the gait and outer feedback at 50 Hz through the existing servo."""

    def __init__(self, cfg, env):
        super().__init__(cfg, env)
        if tuple(self.target_names) != JOINT_NAMES:
            raise ValueError("Hybrid controller requires the contract's joint order")
        self.target_saturated = torch.zeros_like(self._raw_actions, dtype=torch.bool)

    def process_actions(self, actions):
        shift = self.cfg.phase_advance_s / self.cfg.period_s
        desired = joint_reference(self._env, self.cfg.reference, shift)
        ahead = joint_reference(self._env, self.cfg.reference, shift + 1 / SAMPLES)
        behind = joint_reference(self._env, self.cfg.reference, shift - 1 / SAMPLES)
        velocity = (ahead - behind) / (2 * self.cfg.period_s / SAMPLES)
        actual = self._entity.data.joint_pos[:, self._target_ids]
        nominal = (
            desired
            + self.cfg.tracking_gain * (desired - actual)
            + self.cfg.velocity_lead_s * velocity
        )
        target, accepted, saturated = bounded_residual(
            nominal, actions, self._clip[:, :, 0], self._clip[:, :, 1], self._scale
        )
        self._processed_actions = target
        self._raw_actions[:] = accepted
        self.target_saturated[:] = saturated

    def reset(self, env_ids=None):
        super().reset(env_ids)
        self.target_saturated[env_ids] = False


@dataclass(kw_only=True)
class CrawlResidualActionCfg(JointPositionActionCfg):
    """Serialize the complete hybrid controller alongside each checkpoint."""

    reference: list
    period_s: float
    tracking_gain: float
    phase_advance_s: float
    velocity_lead_s: float

    def build(self, env):
        return CrawlResidualAction(self, env)


def residual_cost(env):
    """Prefer small corrections without preventing balance/load compensation."""
    return env.action_manager.action.square().mean(-1)
