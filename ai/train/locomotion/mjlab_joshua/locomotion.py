"""Low-speed flat-ground walking task, extending the standing foundation."""

import math

import torch
from mjlab.managers import CurriculumTermCfg, ObservationTermCfg, RewardTermCfg
from mjlab.sensor import ContactMatch, ContactSensorCfg

from .contract import LEGS
from .task import make_env_cfg, make_rl_cfg

TASK_ID = "Joshua-Spider-Crawl-v1"
GAIT_PERIOD = 0.8
STAGES = (
    (0, (0.0, 0.0), 1.0),
    (50 * 24, (0.02, 0.03), 0.2),
    (250 * 24, (0.03, 0.05), 0.1),
    (1000 * 24, (0.05, 0.05), 0.1),
)


def walking_contract(base):
    """Version the added clock inputs without changing the standing interface."""
    return {
        **base,
        "schema_version": 2,
        "task_id": TASK_ID,
        "actor_terms": {**base["actor_terms"], "gait_clock": 2},
        "actor_dim": 47,
        "critic_dim": 50,
        "gait_clock": {
            "period_s": GAIT_PERIOD,
            "encoding": ["sin(2*pi*phase)", "cos(2*pi*phase)"],
            "phase": (
                "episode_policy_steps * policy_dt / period modulo 1; "
                "standing phase 0"
            ),
            "swing_order": ["fl", "rr", "fr", "rl"],
            "duty_factor": 0.75,
        },
    }


def gait_phase(env):
    """Use a reproducible policy-clock phase; no simulation-only actor state."""
    period_steps = round(GAIT_PERIOD / env.step_dt)
    phase = (env.episode_length_buf % period_steps).float() / period_steps
    command = env.command_manager.get_command("twist")
    return phase * (torch.linalg.vector_norm(command[:, :2], dim=-1) > 0.01)


def gait_clock(env):
    """Append sine/cosine clock values to hardware-observable proprioception."""
    angle = gait_phase(env) * math.tau
    return torch.stack((angle.sin(), angle.cos()), dim=-1)


def swing_mask(env):
    """One-leg-at-a-time crawl in FL, RR, FR, RL order."""
    offsets = torch.tensor([0, 0.5, 0.75, 0.25], device=env.device)
    phase = (gait_phase(env)[:, None] - offsets) % 1.0
    moving = env.command_manager.get_command("twist")[:, 0].abs() > 0.01
    return (phase < 0.25) & moving[:, None]


def gait_contact(env):
    """Reward contact agreement with the crawl schedule, not noisy leg waving."""
    contact, _, _ = feet_state(env)
    return (contact == ~swing_mask(env)).float().mean(-1)


def swing_lift_cost(env):
    """Guide swing lift within the existing action envelope; leave stance/yaw free."""
    offsets = torch.tensor([0, 0.5, 0.75, 0.25], device=env.device)
    phase = (gait_phase(env)[:, None] - offsets) % 1.0
    swing = swing_mask(env)
    lift = (phase.clamp(max=0.25) * (math.pi / 0.25)).sin() * swing
    target = torch.stack((-lift, -0.5 * lift), dim=-1)
    # Preserve reward differences outside the physical action bounds.
    # BoundedPositionAction still clips accepted controls and feedback to ±1.
    actions = env.action_manager.action.reshape(-1, 4, 3)
    error = (actions[:, :, 1:] - target).square().sum(-1)
    return (error * swing).sum(-1)


def track_planar_velocity(env):
    """Separate tight horizontal tracking from vertical bobbing during foot lift."""
    command = env.command_manager.get_command("twist")[:, :2]
    actual = env.scene["robot"].data.root_link_lin_vel_b[:, :2]
    return torch.exp(-(command - actual).square().sum(-1) / 0.03**2)


def vertical_velocity(env):
    """Penalize vertical velocity without suppressing horizontal tracking credit."""
    return env.scene["robot"].data.root_link_lin_vel_b[:, 2].square()


def velocity_error_cost(env):
    """Penalize fast reverse/sideways motion that bounded tracking can ignore."""
    command = env.command_manager.get_command("twist")[:, :2]
    actual = env.scene["robot"].data.root_link_lin_vel_b[:, :2]
    return (command - actual).square().sum(-1) / 0.05**2


def yaw_rate_cost(env):
    """Reject circling while tracking forward velocity in the rotating body frame."""
    command = env.command_manager.get_command("twist")[:, 2]
    actual = env.scene["robot"].data.root_link_ang_vel_b[:, 2]
    return ((actual - command) / 0.25).square()


def action_bounds_cost(env):
    """Keep raw predictions near the usable interval, including free yaw controls."""
    return (env.action_manager.action.abs() - 1).clamp(min=0).square().sum(-1)


def command_curriculum(env, env_ids):
    """Set absolute command ranges from the persisted policy-step counter."""
    del env_ids
    command = env.command_manager.get_term("twist")
    for step, forward, standing in STAGES:
        if env.common_step_counter >= step:
            command.cfg.ranges.lin_vel_x = forward
            command.cfg.rel_standing_envs = standing
    return {"forward_max": torch.tensor(command.cfg.ranges.lin_vel_x[1])}


def feet_state(env):
    """Return ordered foot contact, clearance, and planar speed on flat terrain."""
    robot = env.scene["robot"]
    ids, _ = robot.find_sites(tuple(f"{leg}_foot" for leg in LEGS), preserve_order=True)
    contact = env.scene["feet_ground"].data.found > 0
    geom_ids, _ = robot.find_geoms("fl_foot_collision")
    radius = float(robot.indexing.geoms[geom_ids[0]].size[0])
    height = robot.data.site_pos_w[:, ids, 2] - radius - env.scene.env_origins[:, 2:3]
    speed = torch.linalg.vector_norm(robot.data.site_lin_vel_w[:, ids, :2], dim=-1)
    return contact, height, speed


def foot_slip(env):
    """Penalize planar motion of feet that are actually in contact."""
    contact, _, speed = feet_state(env)
    return (speed.square() * contact).sum(-1)


def swing_clearance(env):
    """Reward moving swing feet near 12 mm clearance, gated by walking commands."""
    contact, height, _ = feet_state(env)
    command = env.command_manager.get_command("twist")
    active = torch.linalg.vector_norm(command[:, :2], dim=-1) > 0.01
    quality = torch.exp(-((height - 0.012) / 0.008).square())
    velocity = env.scene["robot"].data.root_link_lin_vel_b[:, :2]
    progress = (
        (velocity * command[:, :2]).sum(-1)
        / command[:, :2].square().sum(-1).clamp(min=1e-6)
    ).clamp(0, 1)
    return (quality * swing_mask(env) * ~contact).sum(-1) * active * progress


def unsupported(env):
    """Discourage hopping or flight as a substitute for supported walking."""
    contact, _, _ = feet_state(env)
    return (contact.sum(-1) < 2).float()


def effort(env):
    """Squared actuator torque in Nm², using the simulated force actually applied."""
    return env.scene["robot"].data.actuator_force.square().sum(-1)


def power(env):
    """Absolute mechanical joint power; not electrical or thermal power."""
    robot = env.scene["robot"]
    return (robot.data.actuator_force * robot.data.joint_vel).abs().sum(-1)


def overspeed(env):
    """Soft penalty above the CAD's provisional 2 rad/s speed reference."""
    return (env.scene["robot"].data.joint_vel.abs() - 2.0).clamp(min=0).square().sum(-1)


def height_error(env):
    """Keep support height near the settled standing height rather than crouching."""
    height = env.scene["robot"].data.root_link_pos_w[:, 2] - env.scene.env_origins[:, 2]
    return ((height - 0.079) / 0.025).square()


def make_walk_cfg(directory=None, num_envs=512, evaluation=False, speed=0.05):
    """Build training curriculum or a fixed-command evaluation task."""
    cfg = make_env_cfg(directory, num_envs)
    cfg.scene.sensors += (
        ContactSensorCfg(
            name="feet_ground",
            primary=ContactMatch(
                mode="geom",
                pattern=tuple(f"{leg}_foot_collision" for leg in LEGS),
                entity="robot",
            ),
            secondary=ContactMatch(mode="body", pattern="terrain"),
            fields=("found", "force"),
            reduce="netforce",
            num_slots=1,
            track_air_time=True,
        ),
    )
    cfg.rewards["track_linear_velocity"] = RewardTermCfg(
        func=track_planar_velocity, weight=4.0
    )
    cfg.rewards["track_angular_velocity"].params["std"] = 0.25
    cfg.rewards["track_angular_velocity"].weight = 0.5
    cfg.rewards["upright"].weight = 1.0
    cfg.rewards["posture"].weight = -0.1
    cfg.rewards["action_rate_l2"].weight = -0.02
    for name, func, weight in (
        ("slip", foot_slip, -10.0),
        ("swing_clearance", swing_clearance, 0.5),
        ("unsupported", unsupported, -1.5),
        ("effort", effort, -0.1),
        ("power", power, -0.05),
        ("overspeed", overspeed, -0.05),
        ("height", height_error, -0.5),
        ("gait_contact", gait_contact, 2.0),
        ("vertical_velocity", vertical_velocity, -2.0),
        ("swing_lift", swing_lift_cost, -0.5),
        ("velocity_error", velocity_error_cost, -2.0),
        ("yaw_rate", yaw_rate_cost, -1.0),
        ("action_bounds", action_bounds_cost, -0.2),
    ):
        cfg.rewards[name] = RewardTermCfg(func=func, weight=weight)
    cfg.observations["actor"].terms["gait_clock"] = ObservationTermCfg(func=gait_clock)
    cfg.observations["critic"].terms = {
        **cfg.observations["actor"].terms,
        "base_lin_vel": cfg.observations["critic"].terms["base_lin_vel"],
    }
    cfg.episode_length_s = 10.0
    cfg.events["reset_base"].params["pose_range"] = {
        "roll": (-0.02, 0.02),
        "pitch": (-0.02, 0.02),
        "yaw": (-0.05, 0.05),
    }
    cfg.events["reset_robot_joints"].params["position_range"] = (-0.01, 0.01)
    cfg.curriculum = {"speed": CurriculumTermCfg(func=command_curriculum)}
    if evaluation:
        cfg.curriculum = {}
        cfg.commands["twist"].ranges.lin_vel_x = (speed, speed)
        cfg.commands["twist"].rel_standing_envs = 0.0
        cfg.commands["twist"].resampling_time_range = (1000, 1000)
    return cfg


def walk_rl_cfg():
    """Use the same policy architecture/contract with a bounded training schedule."""
    cfg = make_rl_cfg()
    cfg.experiment_name = "spider_walk"
    cfg.max_iterations = 2000
    cfg.save_interval = 100
    cfg.actor.distribution_cfg["init_std"] = 0.5
    cfg.clip_actions = None
    return cfg
