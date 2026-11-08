"""Reward and evaluation checks that catch stationary or contact-cheating policies."""

import os
from types import SimpleNamespace

import pytest
import torch
from mjlab_joshua import locomotion as walk
from mjlab_joshua.evaluate import qualifies, reset_cases, successful


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
            root_link_ang_vel_b=torch.zeros((1, 3)),
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


def test_yaw_and_raw_bounds_costs_reject_circling_and_saturation():
    """Correct body speed alone cannot justify rotation or out-of-range predictions."""
    env = fake_env()
    assert walk.yaw_rate_cost(env).item() == 0
    env.scene["robot"].data.root_link_ang_vel_b[:, 2] = 0.5
    assert walk.yaw_rate_cost(env).item() == pytest.approx(25)
    actions = torch.zeros((1, 12))
    env.action_manager = SimpleNamespace(action=actions)
    assert walk.action_bounds_cost(env).item() == 0
    actions[0, 0] = 2
    assert walk.action_bounds_cost(env).item() == pytest.approx(1)


def test_full_gate_rejects_short_small_or_sampled_evaluations():
    """Keep the 90/100 boundary exact and separate smokes from qualification."""
    assert qualifies(90, 100, 30, 0.05)
    assert not qualifies(89, 100, 30, 0.05)
    assert not qualifies(16, 16, 30, 0.05)
    assert not qualifies(100, 100, 8, 0.05)
    assert not qualifies(100, 100, 30, 0.03)
    assert not qualifies(100, 100, 30, 0.05, sampled=True)


def test_v2_support_rejects_dragging_and_flight():
    """All-ground dragging and unsupported flight must earn no moving gait score."""
    from mjlab_joshua.gait import support_reward

    env = fake_env()
    assert support_reward(env).item() == 0
    env.scene["feet_ground"].data.found[0, 0] = False
    assert support_reward(env).item() == pytest.approx(1)
    env.scene["feet_ground"].data.found[:] = False
    assert support_reward(env).item() == 0
    assert support_reward(fake_env(command=0)).item() == 1


def test_v2_curve_has_smooth_landing_and_planted_stance():
    """Avoid discontinuous position or velocity at lift-off and touchdown."""
    from mjlab_joshua.gait import CLEARANCE, foot_curve

    phase = torch.tensor([0.0, 0.125, 0.25, 0.999999], dtype=torch.float64)
    x, z = foot_curve(phase, 0.05)
    torch.testing.assert_close(x[:3], torch.tensor([-0.015, 0, 0.015], dtype=x.dtype))
    assert abs(float(x[-1] - x[0])) < 1e-6
    assert float(z[1]) == pytest.approx(CLEARANCE)
    assert z[[0, 2, 3]].abs().max() < 1e-12
    for boundary in (0.0, 0.25):
        epsilon = 1e-6
        t = torch.tensor(
            [(boundary - epsilon) % 1, (boundary + epsilon) % 1], dtype=torch.float64
        )
        px, pz = foot_curve(t, 0.05)
        assert float((px[1] - px[0]) / (2 * epsilon * 0.8)) == pytest.approx(
            -0.05, abs=1e-4
        )
        assert abs(float((pz[1] - pz[0]) / (2 * epsilon * 0.8))) < 1e-4


def test_v2_reference_is_reachable_bounded_and_stops_with_command():
    """Check CAD IK feasibility and that the guide does not step while standing."""
    from mjlab_joshua.gait import joint_reference, reference_table

    ref = reference_table(os.environ["JOSHUA_SPIDER_ASSET"])
    assert ref["max_ik_error_m"] < 0.0001
    assert torch.tensor(ref["table"]).abs().max() <= 0.200001
    env = fake_env()
    first = joint_reference(env, ref["table"])
    assert first.shape == (1, 12)
    env.episode_length_buf[:] = 40
    torch.testing.assert_close(joint_reference(env, ref["table"]), first)
    assert joint_reference(fake_env(command=0), ref["table"]).abs().max() == 0


def test_v2_contract_and_objectives_are_separate_from_baseline():
    """Keep v1 checkpoint semantics while giving v2 measured-foot objectives."""
    from mjlab_joshua.contract import load_contract

    asset = os.environ["JOSHUA_SPIDER_ASSET"]
    base = load_contract(asset)
    old, new = walk.walking_contract(base), walk.walking_contract(base, 2)
    assert old["task_id"] == "Joshua-Spider-Crawl-v1"
    assert new["task_id"] == "Joshua-Spider-Crawl-v2"
    assert old["action_scale_rad"] == new["action_scale_rad"] == 0.2
    assert walk.gait_version_from_contract(old) == 1
    assert walk.gait_version_from_contract(new) == 2
    cfg = walk.make_walk_cfg(asset, 4, gait_version=2)
    assert "swing_lift" not in cfg.rewards
    assert "gait_contact" not in cfg.rewards
    assert cfg.rewards["gait_support"].weight > 0
    assert cfg.rewards["failure"].weight < 0
    from mjlab.envs import mdp

    env = SimpleNamespace(
        termination_manager=SimpleNamespace(terminated=torch.tensor([False, True]))
    )
    assert mdp.is_terminated(env).tolist() == [0, 1]
    for term in ("foot_height", "foot_placement", "joint_path", "stance_slip"):
        assert cfg.rewards[term].weight < 0


def test_gait_gate_rejects_one_dragged_leg_and_stationary_stepping():
    """A good mean clearance cannot hide one unused foot or failed forward travel."""
    from mjlab_joshua.evaluate import gait_successful

    walking = torch.tensor([True, True, False, True, True])
    air = torch.full((5, 4), 0.6)
    air[1, 1] = 0
    clearance = torch.full((5, 4), 0.008)
    clearance[3, 2] = 0.001
    support = torch.full((5,), 0.9)
    support[4] = 0.5
    samples = torch.ones((5, 4))
    assert gait_successful(walking, air, clearance, support, samples).tolist() == [
        True,
        False,
        False,
        False,
        False,
    ]


def test_wider_gait_respects_cad_limits_and_uses_its_own_clock():
    """A changed action envelope and cadence must propagate to every consumer."""
    from mjlab_joshua.contract import load_contract
    from mjlab_joshua.gait import reference_table, settings

    asset = os.environ["JOSHUA_SPIDER_ASSET"]
    base = load_contract(asset)
    contract = walk.walking_contract(base, 3)
    cfg = walk.make_walk_cfg(asset, 4, gait_version=3)
    assert contract["action_scale_rad"] == cfg.actions["joint_pos"].scale == 0.3
    assert contract["gait_clock"]["period_s"] == 1.0
    for name, (lower, upper) in zip(base["joint_names"], base["joint_limits_rad"]):
        lo, hi = cfg.actions["joint_pos"].clip[name]
        assert lower <= lo < hi <= upper
    env = fake_env()
    env.cfg = cfg
    env.episode_length_buf[:] = 25
    torch.testing.assert_close(
        walk.gait_clock(env), torch.tensor([[0.0, -1.0]]), atol=1e-6, rtol=0
    )
    env.episode_length_buf[:] = 50
    assert walk.gait_phase(env).item() == 0
    reference = reference_table(asset, **settings(3))
    assert reference["max_ik_error_m"] < 0.0001
    assert 0.2 < float(torch.tensor(reference["table"]).abs().max()) <= 0.3


def test_wider_crawl_rejects_two_leg_bouncing():
    """Enforce the intended support pattern instead of rewarding a partial match."""
    from mjlab_joshua.gait import missing_support_cost, strict_support_reward

    env = fake_env()
    assert strict_support_reward(env).item() == 0
    contact = env.scene["feet_ground"].data.found
    contact[0, 0] = False
    assert strict_support_reward(env).item() == 1
    assert missing_support_cost(env).item() == 0
    contact[0, 1] = False
    assert strict_support_reward(env).item() == 0
    assert missing_support_cost(env).item() == 1


def test_measured_joint_guide_allows_servo_compensation():
    """Correct actual positions earn full path credit despite offset commands."""
    from mjlab_joshua.gait import joint_path_cost, joint_position_path_cost

    env = fake_env()
    robot = env.scene["robot"]
    robot.find_joints = lambda *args, **kwargs: (list(range(12)), [])
    robot.data.joint_pos = torch.full((1, 12), 0.1)
    env.action_manager = SimpleNamespace(action=torch.full((1, 12), 0.8))
    table = torch.full((3, 80, 4, 3), 0.1).tolist()
    assert joint_position_path_cost(env, table, 0.3).item() == pytest.approx(0)
    assert joint_path_cost(env, table, 0.3).item() > 0
    robot.data.joint_pos[:] = 0
    assert joint_position_path_cost(env, table, 0.3).item() > 0


def test_measured_joint_recipe_preserves_previous_checkpoint_behavior():
    """Version the corrected guide while keeping v3 evaluation reproducible."""
    from mjlab_joshua.contract import load_contract
    from mjlab_joshua.gait import joint_path_cost, joint_position_path_cost

    asset = os.environ["JOSHUA_SPIDER_ASSET"]
    base = load_contract(asset)
    previous = walk.make_walk_cfg(asset, 4, gait_version=3)
    corrected = walk.make_walk_cfg(asset, 4, gait_version=4)
    assert previous.rewards["joint_path"].func is joint_path_cost
    assert corrected.rewards["joint_path"].func is joint_position_path_cost
    old, new = walk.walking_contract(base, 3), walk.walking_contract(base, 4)
    assert old["action_scale_rad"] == new["action_scale_rad"] == 0.3
    assert old != new
    assert walk.gait_version_from_contract(new) == 4


def test_ik_neutral_crossings_do_not_report_false_unreachable_poses():
    """Tiny warm starts previously stalled at a 3.125 mm reachable displacement."""
    from mjlab_joshua.gait import reference_table

    reference = reference_table(
        os.environ["JOSHUA_SPIDER_ASSET"],
        period=1.0,
        clearance=0.025,
        action_scale=0.4,
    )
    assert reference["max_ik_error_m"] < 1e-4
    assert torch.tensor(reference["table"]).abs().max() < 0.4


def test_hybrid_residual_clipping_reports_accepted_correction():
    """Feedback must describe the residual actually applied at target limits."""
    from mjlab_joshua.hybrid import bounded_residual

    nominal = torch.tensor([[0.6, -0.6, 0.38, 0.1]])
    actions = torch.tensor([[2.0, -2.0, 1.0, -0.5]])
    target, accepted, saturated = bounded_residual(nominal, actions, -0.4, 0.4, 0.08)
    torch.testing.assert_close(target, torch.tensor([[0.4, -0.4, 0.4, 0.06]]))
    torch.testing.assert_close(accepted, torch.tensor([[0.0, 0.0, 0.25, -0.5]]))
    assert saturated.tolist() == [[True, True, True, False]]
    assert accepted.abs().max() <= 1


def test_hybrid_contract_distinguishes_residual_and_physical_target_limits():
    """A v5 actor must never be loaded as an absolute-position v4 controller."""
    from mjlab_joshua.contract import load_contract
    from mjlab_joshua.hybrid import CrawlResidualActionCfg

    asset = os.environ["JOSHUA_SPIDER_ASSET"]
    base = load_contract(asset)
    new = walk.walking_contract(base, 5)
    cfg = walk.make_walk_cfg(asset, 4, gait_version=5)
    assert new["actor_dim"] == 47
    assert new["action_scale_rad"] == cfg.actions["joint_pos"].scale == 0.08
    assert new["target_limit_rad"] == 0.4
    assert new != walk.walking_contract(base, 4)
    assert walk.gait_version_from_contract(new) == 5
    assert isinstance(cfg.actions["joint_pos"], CrawlResidualActionCfg)
    for name, (lower, upper) in zip(base["joint_names"], base["joint_limits_rad"]):
        lo, hi = cfg.actions["joint_pos"].clip[name]
        assert lower <= lo < hi <= upper
    assert "joint_path" not in cfg.rewards


def test_shifted_gait_reference_wraps_and_stops_at_zero_command():
    """Phase lead must wrap cyclically without moving a commanded standing robot."""
    from mjlab_joshua.gait import joint_reference, reference_table, settings

    reference = reference_table(os.environ["JOSHUA_SPIDER_ASSET"], **settings(5))
    env = fake_env()
    env.cfg = walk.make_walk_cfg(os.environ["JOSHUA_SPIDER_ASSET"], 4, gait_version=5)
    torch.testing.assert_close(
        joint_reference(env, reference["table"], 0.04),
        joint_reference(env, reference["table"], 1.04),
        atol=1e-6,
        rtol=0,
    )
    env.command_manager.get_command = lambda _: torch.zeros((1, 3))
    assert joint_reference(env, reference["table"], 0.04).abs().max() == 0
