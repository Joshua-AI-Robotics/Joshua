"""Reachable foot trajectories and support objectives for versioned crawls."""

from functools import lru_cache
from pathlib import Path

import mujoco
import numpy as np
import torch
from mjlab.envs import mdp
from mjlab.managers import RewardTermCfg
from mjlab.utils.lab_api.math import quat_apply_inverse
from scipy.optimize import least_squares

from .contract import ACTION_SCALE, JOINT_NAMES, LEGS

PERIOD = 0.8
DUTY = 0.75
CLEARANCE = 0.01
SAMPLES = 80
SPEEDS = (0.0, 0.025, 0.05)
OFFSETS = (0.0, 0.5, 0.75, 0.25)


def settings(version=2):
    """Keep the narrow comparison recipe and the wider, slower gait explicit."""
    if version == 2:
        return {"period": PERIOD, "clearance": CLEARANCE, "action_scale": ACTION_SCALE}
    if version in (3, 4):
        return {"period": 1.0, "clearance": 0.012, "action_scale": 0.3}
    if version == 5:
        return {"period": 1.0, "clearance": 0.025, "action_scale": 0.4}
    raise ValueError("Unknown foot-path recipe")


def foot_curve(phase, speed, period=PERIOD, clearance=CLEARANCE):
    """Smooth swing with stance-matched endpoint velocity and zero vertical speed."""
    swing = phase < 1 - DUTY
    t = (phase / (1 - DUTY)).clamp(max=1)
    stride = speed * period * DUTY
    smooth = 3 * t.square() - 2 * t.pow(3)
    hermite = 2 * t.pow(3) - 3 * t.square() + t
    x_swing = stride * (smooth - 0.5) - speed * period * (1 - DUTY) * hermite
    x_stance = stride / 2 - speed * period * (phase - (1 - DUTY))
    z = clearance * torch.sin(torch.pi * t).square() * swing
    return torch.where(swing, x_swing, x_stance), z


def leg_phase(env):
    """Per-leg phase in the existing FL, RR, FR, RL schedule."""
    from .locomotion import gait_phase

    return (gait_phase(env)[:, None] - torch.tensor(OFFSETS, device=env.device)) % 1


@lru_cache(maxsize=4)
def reference_table(
    asset, period=PERIOD, clearance=CLEARANCE, action_scale=ACTION_SCALE
):
    """Solve generated CAD kinematics within the chosen target envelope."""
    model = mujoco.MjModel.from_xml_path(str(Path(asset) / "robot.xml"))
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)
    sites = [model.site(f"{leg}_foot").id for leg in LEGS]
    home = data.site_xpos[sites].copy()
    table = np.zeros((len(SPEEDS), SAMPLES, 4, 3))
    max_error = 0.0
    phase = torch.arange(SAMPLES, dtype=torch.float64) / SAMPLES
    for speed_index, speed in enumerate(SPEEDS):
        x, z = foot_curve(phase, speed, period, clearance)
        for leg_index, leg in enumerate(LEGS):
            addresses = [
                int(model.joint(name).qposadr[0])
                for name in JOINT_NAMES
                if name.startswith(leg + "_")
            ]
            q = np.zeros(3)
            for tick in range(SAMPLES):
                target = home[leg_index] + [float(x[tick]), 0, float(z[tick])]

                def error(position):
                    data.qpos[addresses] = position
                    mujoco.mj_forward(model, data)
                    return data.site_xpos[sites[leg_index]] - target

                solution = least_squares(
                    error,
                    # A tiny nonzero warm start gives TRF a tiny initial trust
                    # region. At neutral crossings it can stop on ftol before
                    # taking a useful step. Exact zero uses its unit fallback.
                    np.zeros(3) if np.linalg.norm(q) < 1e-6 else q,
                    bounds=(-action_scale, action_scale),
                    gtol=1e-10,
                    ftol=1e-10,
                    xtol=1e-10,
                )
                q = solution.x
                max_error = max(max_error, float(np.linalg.norm(solution.fun)))
                table[speed_index, tick, leg_index] = q
            data.qpos[addresses] = 0
    if max_error > 0.0001:
        raise ValueError(f"Crawl reference is unreachable: {max_error} m error")
    radius = float(model.geom("fl_foot_collision").size[0])
    return {
        "table": table.tolist(),
        "home": home.tolist(),
        "base_height": radius - float(home[:, 2].mean()),
        "max_ik_error_m": max_error,
        "max_reference_speed_rad_s": float(
            np.max(
                np.abs(np.diff(np.concatenate([table, table[:, :1]], axis=1), axis=1))
            )
            / (period / SAMPLES)
        ),
    }


def joint_reference(env, table, phase_shift=0.0):
    """Interpolate the IK guide in phase and commanded speed on the GPU."""
    if not hasattr(env, "_crawl_reference"):
        env._crawl_reference = torch.tensor(table, device=env.device)
    reference = env._crawl_reference
    phase = ((leg_phase(env) + phase_shift) % 1) * SAMPLES
    low = phase.long() % SAMPLES
    alpha = (phase - phase.floor())[..., None]
    speed = env.command_manager.get_command("twist")[:, 0]
    coordinate = (speed / 0.025).clamp(0, len(SPEEDS) - 1)
    lower = coordinate.long().clamp(max=len(SPEEDS) - 2)
    blend = (coordinate - lower)[:, None, None]
    legs = torch.arange(4, device=env.device)[None, :]

    def sample(index):
        before = reference[index[:, None], low, legs]
        after = reference[index[:, None], (low + 1) % SAMPLES, legs]
        return before + alpha * (after - before)

    q = sample(lower) * (1 - blend) + sample(lower + 1) * blend
    return q.flatten(1) * (speed > 0.01)[:, None]


def joint_path_cost(env, table, action_scale=ACTION_SCALE):
    """Guide all twelve actions using IK instead of guessed hip/knee signs."""
    desired = joint_reference(env, table) / action_scale
    return (env.action_manager.action - desired).square().mean(-1)


def joint_position_path_cost(env, table, action_scale=ACTION_SCALE):
    """Track actual joints, allowing targets to compensate compliant servo lag."""
    robot = env.scene["robot"]
    ids, _ = robot.find_joints(JOINT_NAMES, preserve_order=True)
    desired = joint_reference(env, table)
    return ((robot.data.joint_pos[:, ids] - desired) / action_scale).square().mean(-1)


def support_reward(env):
    """Require the scheduled swing foot airborne AND stance feet supporting."""
    from .locomotion import feet_state, swing_mask

    contact, _, _ = feet_state(env)
    swing = swing_mask(env)
    support = (contact & ~swing).float().sum(-1) / 3
    lifted = (~contact & swing).any(-1)
    moving = swing.any(-1)
    return torch.where(moving, support * lifted, contact.float().mean(-1))


def strict_support_reward(env):
    """A crawl earns gait credit only with all three stance feet in contact."""
    from .locomotion import feet_state, swing_mask

    contact, _, _ = feet_state(env)
    swing = swing_mask(env)
    valid = (contact | swing).all(-1) & (~contact & swing).any(-1)
    return torch.where(swing.any(-1), valid.float(), contact.float().mean(-1))


def missing_support_cost(env):
    """Discourage two-leg bouncing during the three-support-leg crawl."""
    from .locomotion import feet_state

    contact, _, _ = feet_state(env)
    return (3 - contact.sum(-1)).clamp(min=0).float().square()


def foot_height_cost(env, period=PERIOD, clearance=CLEARANCE, swing_weight=1.0):
    """Track measured clearance throughout swing; keep stance feet on the floor."""
    from .locomotion import feet_state

    _, height, _ = feet_state(env)
    speed = env.command_manager.get_command("twist")[:, 0:1]
    _, target = foot_curve(leg_phase(env), speed, period, clearance)
    target *= speed > 0.01
    weights = torch.where(target > 0, swing_weight, 1.0)
    return (((height - target) / clearance).square() * weights).mean(-1)


def foot_placement_cost(env, home, period=PERIOD):
    """Track forward swing and backward stance placement relative to the body."""
    robot = env.scene["robot"]
    ids, _ = robot.find_sites(tuple(f"{leg}_foot" for leg in LEGS), preserve_order=True)
    relative = robot.data.site_pos_w[:, ids] - robot.data.root_link_pos_w[:, None]
    rotation = robot.data.root_link_quat_w[:, None].expand(-1, 4, -1)
    feet = quat_apply_inverse(rotation, relative)
    target = (
        torch.tensor(home, device=env.device)[None, :, :2]
        .expand_as(feet[..., :2])
        .clone()
    )
    speed = env.command_manager.get_command("twist")[:, 0:1]
    x, _ = foot_curve(leg_phase(env), speed, period)
    target[:, :, 0] += x * (speed > 0.01)
    return ((feet[..., :2] - target) / 0.02).square().sum(-1).mean(-1)


def stance_slip_cost(env):
    """Use velocity at the sphere's ground point, excluding rolling contribution."""
    from .locomotion import feet_state

    robot = env.scene["robot"]
    names = tuple(f"{leg}_foot" for leg in LEGS)
    sites, _ = robot.find_sites(names, preserve_order=True)
    bodies, _ = robot.find_bodies(names, preserve_order=True)
    contact, _, _ = feet_state(env)
    geom_ids, _ = robot.find_geoms("fl_foot_collision")
    radius = float(robot.indexing.geoms[geom_ids[0]].size[0])
    omega = robot.data.body_link_ang_vel_w[:, bodies]
    offset = torch.zeros_like(omega)
    offset[..., 2] = -radius
    velocity = robot.data.site_lin_vel_w[:, sites] + torch.cross(omega, offset, dim=-1)
    return (velocity[..., :2].square().sum(-1) * contact).sum(-1) / 0.05**2


def neutral_height_cost(env, target):
    """Use the CAD-derived neutral stance height rather than an arbitrary crouch."""
    height = env.scene["robot"].data.root_link_pos_w[:, 2] - env.scene.env_origins[:, 2]
    return ((height - target) / 0.02).square()


def configure(cfg, asset, version=2):
    """Replace v1's shaping with reachable trajectories and stricter support."""
    from .contract import load_contract

    recipe = settings(version)
    reference = reference_table(str(asset), **recipe)
    scale = recipe["action_scale"]
    cfg.actions["joint_pos"].scale = scale
    cfg.actions["joint_pos"].clip = {
        name: (max(-scale, lower), min(scale, upper))
        for name, (lower, upper) in zip(
            JOINT_NAMES, load_contract(asset)["joint_limits_rad"]
        )
    }
    cfg.observations["actor"].terms["gait_clock"].params = {
        "period_s": recipe["period"]
    }
    for name in ("swing_lift", "swing_clearance", "slip", "gait_contact", "height"):
        del cfg.rewards[name]
    cfg.rewards.update(
        {
            "failure": RewardTermCfg(func=mdp.is_terminated, weight=-100.0),
            "gait_support": RewardTermCfg(func=support_reward, weight=2.0),
            "foot_height": RewardTermCfg(
                func=foot_height_cost,
                weight=-3.0,
                params={"period": recipe["period"], "clearance": recipe["clearance"]},
            ),
            "foot_placement": RewardTermCfg(
                func=foot_placement_cost,
                weight=-1.0,
                params={"home": reference["home"], "period": recipe["period"]},
            ),
            "joint_path": RewardTermCfg(
                func=joint_path_cost,
                weight=-1.0,
                params={"table": reference["table"], "action_scale": scale},
            ),
            "stance_slip": RewardTermCfg(func=stance_slip_cost, weight=-0.15),
            "neutral_height": RewardTermCfg(
                func=neutral_height_cost,
                weight=-0.25,
                params={"target": reference["base_height"]},
            ),
        }
    )
    cfg.rewards["posture"].weight = -0.02
    cfg.rewards["overspeed"].weight = -0.1
    if version in (3, 4, 5):
        cfg.rewards["foot_height"].weight = -6.0
        cfg.rewards["foot_height"].params["swing_weight"] = 3.0
        cfg.rewards["joint_path"].weight = -2.0
        cfg.rewards["gait_support"].func = strict_support_reward
        cfg.rewards["missing_support"] = RewardTermCfg(
            func=missing_support_cost, weight=-2.0
        )
        cfg.rewards["failure"].weight = -500.0
    if version == 4:
        cfg.rewards["joint_path"].func = joint_position_path_cost
    if version == 5:
        from .hybrid import PARAMETERS, CrawlResidualActionCfg, residual_cost

        contract = load_contract(asset)
        cfg.actions["joint_pos"] = CrawlResidualActionCfg(
            entity_name="robot",
            actuator_names=JOINT_NAMES,
            scale=PARAMETERS["residual_scale_rad"],
            use_default_offset=False,
            clip=cfg.actions["joint_pos"].clip,
            reference=reference["table"],
            period_s=recipe["period"],
            tracking_gain=PARAMETERS["tracking_gain"],
            phase_advance_s=PARAMETERS["phase_advance_s"],
            velocity_lead_s=contract["position_kv"] / contract["position_kp"],
        )
        del cfg.rewards["joint_path"]
        cfg.rewards["foot_height"].params["clearance"] = PARAMETERS[
            "measured_clearance_m"
        ]
        cfg.rewards["foot_height"].weight = -10.0
        cfg.rewards["gait_support"].weight = 4.0
        cfg.rewards["missing_support"].weight = -4.0
        cfg.rewards["residual"] = RewardTermCfg(func=residual_cost, weight=-0.02)
    return cfg


def main():
    """Export simultaneous CAD pose samples for the chosen gait and leg corners."""
    import argparse
    import itertools
    import json
    import os

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gait-version", type=int, choices=(2, 3, 4, 5), default=5)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    recipe = settings(args.gait_version)
    reference = reference_table(os.environ["JOSHUA_SPIDER_ASSET"], **recipe)
    table = np.array(reference["table"])[-1]
    poses = []
    for tick in range(0, SAMPLES, 2):
        positions = np.concatenate(
            [
                table[(tick - round(offset * SAMPLES)) % SAMPLES, leg]
                for leg, offset in enumerate(OFFSETS)
            ]
        )
        poses.append(
            {
                "name": f"gait_phase_{tick}/{SAMPLES}",
                "joints": dict(zip(JOINT_NAMES, positions.tolist())),
            }
        )
    for leg in LEGS:
        for index, corner in enumerate(
            itertools.product(
                [-recipe["action_scale"], recipe["action_scale"]], repeat=3
            )
        ):
            positions = dict.fromkeys(JOINT_NAMES, 0.0)
            names = [name for name in JOINT_NAMES if name.startswith(leg + "_")]
            positions.update(zip(names, corner))
            poses.append({"name": f"{leg}_corner_{index}", "joints": positions})
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"recipe": recipe, "poses": poses}, indent=2))
    print(f"Exported {len(poses)} CAD motion samples to {args.output}")


if __name__ == "__main__":
    main()
