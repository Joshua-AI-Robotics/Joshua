"""Reward and evaluation checks that catch stationary or contact-cheating policies."""

import os
from types import SimpleNamespace

import pytest
import torch
from mjlab_joshua import locomotion as walk
from mjlab_joshua.evaluate import reset_cases, successful


def fake_env(contact=True, height=0, speed=0, command=0.05):
    """Provide physical quantities with known values for reward invariants."""
    found = torch.full((1, 4), contact, dtype=torch.bool)
    positions = torch.zeros((1, 4, 3))
    positions[:, :, 2] = height + 0.01
    velocity = torch.zeros_like(positions)
    velocity[:, :, 0] = speed
    robot = SimpleNamespace(
        find_geoms=lambda _: ([0], []),
        indexing=SimpleNamespace(geoms=[SimpleNamespace(size=[0.01])]),
        find_sites=lambda *args, **kwargs: (list(range(4)), []),
        data=SimpleNamespace(
            site_pos_w=positions,
            site_lin_vel_w=velocity,
            root_link_lin_vel_b=torch.tensor([[0.05, 0, 0]]),
        ),
    )
    scene = SimpleScene(
        robot=robot, feet_ground=SimpleNamespace(data=SimpleNamespace(found=found))
    )
    return SimpleNamespace(
        scene=scene,
        device="cpu",
        step_dt=0.02,
        episode_length_buf=torch.tensor([0]),
        command_manager=SimpleNamespace(
            get_command=lambda _: torch.tensor([[command, 0, 0]], dtype=torch.float32)
        ),
    )


class SimpleScene(dict):
    """Expose entity lookup and flat-terrain origins."""

    env_origins = torch.zeros((1, 3))


def test_slip_requires_contact_and_clearance_requires_command():
    """Reward swing clearance; reject stance sliding and stationary leg waving."""
    stance = fake_env(speed=0.1)
    swing = fake_env(contact=False, speed=0.1, height=0.012)
    low_swing = fake_env(contact=False, speed=0.1, height=0)
    standing = fake_env(contact=False, speed=0.1, height=0.012, command=0)
    assert walk.foot_slip(stance).item() == pytest.approx(0.04)
    assert walk.foot_slip(swing).item() == 0
    assert walk.swing_clearance(swing) > walk.swing_clearance(low_swing)
    assert walk.swing_clearance(standing).item() == 0
    assert walk.unsupported(swing).item() == 1
    assert walk.unsupported(stance).item() == 0


def test_curriculum_restores_absolute_stage():
    """A resumed or reset counter must select its stage without accumulated changes."""
    command = SimpleNamespace(
        cfg=SimpleNamespace(ranges=SimpleNamespace(lin_vel_x=None))
    )
    env = SimpleNamespace(command_manager=SimpleNamespace(get_term=lambda _: command))
    for step, expected, standing in (
        (6000, (0.03, 0.05), 0.1),
        (0, (0, 0), 1),
        (1200, (0.02, 0.03), 0.2),
        (24000, (0.05, 0.05), 0.1),
    ):
        env.common_step_counter = step
        walk.command_curriculum(env, None)
        assert command.cfg.ranges.lin_vel_x == expected
        assert command.cfg.rel_standing_envs == standing


def test_evaluation_rejects_standing_and_failed_episodes():
    """A survived episode alone is insufficient evidence of walking."""
    survived = torch.tensor([True, False, True, True])
    rmse = torch.tensor([0.0, 0.0, 0.05, 0.01])
    distance = torch.tensor([0.0, 1.5, 1.5, 1.4])
    result = successful(survived, rmse, distance, required_m=0.84)
    assert result.tolist() == [False, False, False, True]


def test_seeded_cases_independent_of_reset_order():
    """Each episode seed defines its state regardless of GPU batch/reset ordering."""

    class Robot:
        data = SimpleNamespace(
            default_root_state=torch.zeros((3, 13)),
            default_joint_pos=torch.zeros((3, 12)),
        )

        def write_root_state_to_sim(self, root, env_ids):
            self.roots[env_ids] = root

        def write_joint_state_to_sim(self, position, velocity, env_ids):
            self.joints[env_ids] = position

    robot = Robot()
    robot.roots, robot.joints = torch.zeros((3, 13)), torch.zeros((3, 12))
    scene = SimpleScene(robot=robot)
    scene.env_origins = torch.zeros((3, 3))
    env = SimpleNamespace(scene=scene, device="cpu", num_envs=3)
    reset_cases(env, torch.tensor([0, 2]), seed_base=10000)
    first_root, first_joint = robot.roots.clone(), robot.joints.clone()
    reset_cases(env, torch.tensor([2, 0]), seed_base=10000)
    torch.testing.assert_close(robot.roots, first_root)
    torch.testing.assert_close(robot.joints, first_joint)
    assert not torch.equal(robot.joints[0], robot.joints[2])


def test_walking_reward_signs_and_evaluation_command():
    """Keep evaluation commands fixed and velocity tracking tolerance narrow."""
    cfg = walk.make_walk_cfg(os.environ["JOSHUA_SPIDER_ASSET"], 4, evaluation=True)
    assert cfg.curriculum == {}
    assert cfg.commands["twist"].ranges.lin_vel_x == (0.05, 0.05)
    assert cfg.commands["twist"].rel_standing_envs == 0
    assert cfg.commands["twist"].rel_forward_envs == 0
    assert cfg.commands["twist"].init_velocity_prob == 0
    assert cfg.rewards["track_linear_velocity"].func == walk.track_planar_velocity
    assert cfg.rewards["swing_clearance"].weight > 0
    for name in ("slip", "effort", "power", "overspeed", "height", "unsupported"):
        assert cfg.rewards[name].weight < 0


def test_crawl_phase_clock_and_progress_gating():
    """Expose phase to the actor and reject clearance rewards without body progress."""
    env = fake_env(contact=False, speed=0.1, height=0.012)
    expected = (0, 3, 1, 2)
    for step, index in zip((0, 10, 20, 30), expected):
        env.episode_length_buf[:] = step
        swing = walk.swing_mask(env)
        assert int(swing.sum()) == 1
        assert bool(swing[0, index])
    env.episode_length_buf[:] = 0
    first = walk.gait_clock(env)
    env.episode_length_buf[:] = 40
    torch.testing.assert_close(walk.gait_clock(env), first)
    moving_reward = walk.swing_clearance(env)
    env.scene["robot"].data.root_link_lin_vel_b[:] = 0
    assert moving_reward > 0
    assert walk.swing_clearance(env).item() == 0


def test_planar_tracking_separates_vertical_motion():
    """Foot lift must not remove otherwise correct horizontal tracking credit."""
    env = fake_env()
    env.scene["robot"].data.root_link_lin_vel_b[:] = torch.tensor([0.05, 0, 0.1])
    assert walk.track_planar_velocity(env).item() == pytest.approx(1)
    assert walk.vertical_velocity(env).item() > 0
    env.scene["robot"].data.root_link_lin_vel_b[:] = 0
    assert walk.track_planar_velocity(env).item() < 0.1


def test_swing_lift_guidance_leaves_stance_control_free():
    """Give a dense lift signal without prescribing stance/yaw placement."""
    env = fake_env()
    env.episode_length_buf[:] = 5
    actions = torch.zeros((1, 12))
    env.action_manager = SimpleNamespace(action=actions)
    assert walk.swing_lift_cost(env).item() == pytest.approx(1.25)
    actions[0, 1:3] = torch.tensor([-1.0, -0.5])
    assert walk.swing_lift_cost(env).item() == pytest.approx(0)
    actions[0, 3:6] = 0.8
    assert walk.swing_lift_cost(env).item() == pytest.approx(0)
    actions[0, 1] = 2.0
    assert walk.swing_lift_cost(env).item() == pytest.approx(9)
    assert walk.walk_rl_cfg().clip_actions is None


def test_velocity_cost_rejects_forward_backward_rocking():
    """A zero-progress cycle cannot hide its fast backward half in reward saturation."""
    env = fake_env()
    velocity = env.scene["robot"].data.root_link_lin_vel_b
    velocity[:] = 0
    stationary = walk.velocity_error_cost(env).item()
    velocity[:, 0] = 0.05
    forward = walk.velocity_error_cost(env).item()
    velocity[:, 0] = -0.15
    backward = walk.velocity_error_cost(env).item()
    assert forward == pytest.approx(0)
    assert 0.75 * forward + 0.25 * backward > stationary
