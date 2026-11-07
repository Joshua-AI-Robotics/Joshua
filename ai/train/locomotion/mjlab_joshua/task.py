"""Initial standing task and PPO configuration using mjlab 1.3."""

import os
from dataclasses import dataclass
from pathlib import Path

import mujoco
import torch
from mjlab.actuator import BuiltinPositionActuatorCfg
from mjlab.entity import EntityArticulationInfoCfg, EntityCfg
from mjlab.envs import mdp as env_mdp
from mjlab.envs.mdp.actions import JointPositionAction, JointPositionActionCfg
from mjlab.managers import (
    ObservationGroupCfg,
    ObservationTermCfg,
    RewardTermCfg,
    TerminationTermCfg,
)
from mjlab.rl import RslRlModelCfg, RslRlOnPolicyRunnerCfg
from mjlab.sensor import ContactMatch, ContactSensorCfg
from mjlab.tasks.registry import register_mjlab_task
from mjlab.tasks.velocity import mdp
from mjlab.tasks.velocity.mdp import UniformVelocityCommandCfg
from mjlab.tasks.velocity.velocity_env_cfg import make_velocity_env_cfg

from .contract import ACTION_SCALE, ACTOR_TERMS, JOINT_NAMES, TASK_ID, load_contract


class BoundedPositionAction(JointPositionAction):
    """Store clipped normalized actions for matching previous-action feedback."""

    def process_actions(self, actions):
        """Bound input before applying the radian scale and joint limits."""
        super().process_actions(actions.clamp(-1.0, 1.0))


@dataclass(kw_only=True)
class BoundedPositionActionCfg(JointPositionActionCfg):
    """Use the same action semantics with and without the PPO wrapper."""

    def build(self, env):
        """Construct the bounded action term."""
        return BoundedPositionAction(self, env)


def previous_action(env):
    """Return the action actually accepted by the action term."""
    return env.action_manager.get_term("joint_pos").raw_action


def posture_cost(env):
    """Penalize departure from the CAD zero/home pose."""
    data = env.scene["robot"].data
    return torch.sum((data.joint_pos - data.default_joint_pos) ** 2, dim=-1)


def make_env_cfg(directory=None, num_envs=64):
    """Build the foundation task: zero twist, flat floor, no domain randomization."""
    directory = Path(directory or os.environ["JOSHUA_SPIDER_ASSET"])
    contract = load_contract(directory)
    cfg = make_velocity_env_cfg()
    cfg.scene.entities = {
        "robot": EntityCfg(
            spec_fn=lambda: mujoco.MjSpec.from_file(str(directory / "robot.xml")),
            init_state=EntityCfg.InitialStateCfg(
                pos=(0, 0, contract["initial_base_z_m"]),
                joint_pos={".*": 0.0},
            ),
            articulation=EntityArticulationInfoCfg(
                actuators=(
                    BuiltinPositionActuatorCfg(
                        target_names_expr=JOINT_NAMES,
                        stiffness=contract["position_kp"],
                        damping=contract["position_kv"],
                        effort_limit=contract["effort_limit_Nm"],
                    ),
                ),
                soft_joint_pos_limit_factor=0.9,
            ),
        )
    }
    cfg.scene.num_envs = num_envs
    cfg.scene.terrain.terrain_type = "plane"
    cfg.scene.terrain.terrain_generator = None
    cfg.scene.sensors = (
        ContactSensorCfg(
            name="body_ground",
            primary=ContactMatch(
                mode="geom", pattern=r".*(?<!foot)_collision", entity="robot"
            ),
            secondary=ContactMatch(mode="body", pattern="terrain"),
            fields=("found",),
            reduce="none",
            num_slots=1,
        ),
    )
    cfg.sim.mujoco.timestep = contract["physics_dt_s"]
    cfg.sim.nconmax = 64
    cfg.sim.njmax = 256
    cfg.sim.contact_sensor_maxmatch = 64
    cfg.decimation = contract["decimation"]
    cfg.episode_length_s = 10.0
    cfg.viewer.body_name = "base_link"
    cfg.viewer.distance = 0.7
    cfg.events = {k: cfg.events[k] for k in ("reset_base", "reset_robot_joints")}
    cfg.events["reset_base"].params["pose_range"] = {}
    cfg.curriculum = {}
    cfg.metrics = {}
    cfg.actions = {
        "joint_pos": BoundedPositionActionCfg(
            entity_name="robot",
            actuator_names=JOINT_NAMES,
            scale=ACTION_SCALE,
            use_default_offset=True,
            clip={
                name: (max(-ACTION_SCALE, lo), min(ACTION_SCALE, hi))
                for name, (lo, hi) in zip(JOINT_NAMES, contract["joint_limits_rad"])
            },
        )
    }
    actor = {
        "base_ang_vel": ObservationTermCfg(func=mdp.base_ang_vel),
        "projected_gravity": ObservationTermCfg(func=mdp.projected_gravity),
        "joint_pos": ObservationTermCfg(func=mdp.joint_pos_rel),
        "joint_vel": ObservationTermCfg(func=mdp.joint_vel_rel),
        "actions": ObservationTermCfg(func=previous_action),
        "command": ObservationTermCfg(
            func=mdp.generated_commands, params={"command_name": "twist"}
        ),
    }
    assert tuple(actor) == tuple(ACTOR_TERMS)
    cfg.observations = {
        "actor": ObservationGroupCfg(
            terms=actor,
            concatenate_terms=True,
            enable_corruption=False,
        ),
        "critic": ObservationGroupCfg(
            terms={**actor, "base_lin_vel": ObservationTermCfg(func=mdp.base_lin_vel)},
            concatenate_terms=True,
            enable_corruption=False,
        ),
    }
    cfg.commands = {
        "twist": UniformVelocityCommandCfg(
            entity_name="robot",
            resampling_time_range=(3, 5),
            rel_standing_envs=1.0,
            heading_command=False,
            debug_vis=False,
            ranges=UniformVelocityCommandCfg.Ranges(
                lin_vel_x=(0, 0),
                lin_vel_y=(0, 0),
                ang_vel_z=(0, 0),
            ),
        )
    }
    cfg.rewards = {
        key: cfg.rewards[key]
        for key in (
            "track_linear_velocity",
            "track_angular_velocity",
            "upright",
            "dof_pos_limits",
            "action_rate_l2",
        )
    }
    cfg.rewards["upright"].params["asset_cfg"].body_names = ("base_link",)
    cfg.rewards["posture"] = RewardTermCfg(func=posture_cost, weight=-0.5)
    cfg.terminations = {
        "time_out": TerminationTermCfg(func=env_mdp.time_out, time_out=True),
        "fell_over": TerminationTermCfg(
            func=env_mdp.bad_orientation,
            params={"limit_angle": 0.7},
        ),
        "too_low": TerminationTermCfg(
            func=env_mdp.root_height_below_minimum,
            params={"minimum_height": 0.04},
        ),
        "body_contact": TerminationTermCfg(
            func=mdp.illegal_contact,
            params={"sensor_name": "body_ground"},
        ),
    }
    return cfg


def make_rl_cfg():
    """Small normalized PPO network; defaults intentionally stop after the smoke."""
    return RslRlOnPolicyRunnerCfg(
        actor=RslRlModelCfg(
            hidden_dims=(128, 128),
            obs_normalization=True,
            distribution_cfg={
                "class_name": "GaussianDistribution",
                "init_std": 0.3,
                "std_type": "scalar",
            },
        ),
        critic=RslRlModelCfg(hidden_dims=(128, 128), obs_normalization=True),
        experiment_name="spider_stand",
        logger="tensorboard",
        upload_model=False,
        clip_actions=1.0,
        num_steps_per_env=24,
        max_iterations=5,
        save_interval=5,
    )


def register():
    """Register the generated asset explicitly, after preparation."""
    register_mjlab_task(
        task_id=TASK_ID,
        env_cfg=make_env_cfg(),
        play_env_cfg=make_env_cfg(num_envs=1),
        rl_cfg=make_rl_cfg(),
    )
