"""Physics and interface gates independent of reward optimization or hardware."""

import hashlib
import json
import os
import xml.etree.ElementTree as ET
from pathlib import Path
from types import SimpleNamespace

import mujoco
import numpy as np
import pytest
import torch
from mjlab_joshua.contract import ACTOR_TERMS, JOINT_NAMES, load_contract
from mjlab_joshua.task import make_env_cfg, make_rl_cfg, posture_cost

ASSET = Path(os.environ["JOSHUA_SPIDER_ASSET"])
PROJECT = Path("/workspace/output/robotcad/spider_sts3215")


def scene():
    """Compile the actual training body with a ground plane and position servos."""
    root = ET.parse(ASSET / "robot.xml").getroot()
    contract = load_contract(ASSET)
    root.find("worldbody/body").set("pos", f"0 0 {contract['initial_base_z_m']}")
    ET.SubElement(root, "option", timestep="0.001", integrator="implicitfast")
    ET.SubElement(
        root.find("worldbody"),
        "geom",
        name="floor",
        type="plane",
        size="1 1 0.1",
        contype="1",
        conaffinity="2",
    )
    actuators = ET.SubElement(root, "actuator")
    for joint in JOINT_NAMES:
        ET.SubElement(
            actuators,
            "position",
            joint=joint,
            kp=str(contract["position_kp"]),
            kv=str(contract["position_kv"]),
            forcerange=f"{-contract['effort_limit_Nm']} {contract['effort_limit_Nm']}",
        )
    return mujoco.MjModel.from_xml_string(ET.tostring(root, encoding="unicode"))


def test_cad_frames_and_inertias_preserved():
    """Simplification must not silently change the kinematics or mass model."""
    original = mujoco.MjModel.from_xml_path(str(PROJECT / "mujoco/robot.xml"))
    model = scene()
    assert (model.nq, model.nv, model.nu) == (19, 18, 12)
    assert model.nmesh == 0
    assert model.nbody == original.nbody
    for field in ("body_mass", "body_inertia", "body_ipos", "body_iquat", "jnt_range"):
        np.testing.assert_allclose(getattr(model, field), getattr(original, field))
    data, cad_data = mujoco.MjData(model), mujoco.MjData(original)
    mujoco.mj_forward(model, data)
    mujoco.mj_forward(original, cad_data)
    np.testing.assert_allclose(data.xpos, cad_data.xpos, atol=1e-9)
    np.testing.assert_allclose(data.xmat, cad_data.xmat, atol=1e-9)
    for leg in ("fl", "fr", "rl", "rr"):
        foot = model.geom(f"{leg}_foot_collision")
        assert foot.type == mujoco.mjtGeom.mjGEOM_SPHERE
        assert foot.contype == 2 and foot.conaffinity == 1


def test_three_second_standing_and_fallen_body_contacts():
    """Require stable zero-target support and a body that can collide when fallen."""
    model = scene()
    data = mujoco.MjData(model)
    for _ in range(3000):
        mujoco.mj_step(model, data)
        assert all(
            any("foot_collision" in model.geom(i).name for i in c.geom)
            for c in data.contact
        ), "Non-foot contact during home-pose landing/settling"
    assert np.isfinite(data.qpos).all()
    assert data.qpos[2] > 0.04
    base = model.body("base_link").id
    assert data.xmat[base].reshape(3, 3)[2, 2] > 0.9
    feet = set()
    for contact in data.contact:
        for index in contact.geom:
            name = model.geom(index).name
            if "foot_collision" in name:
                feet.add(name)
    assert len(feet) == 4
    assert np.max(np.abs(data.actuator_force)) <= 0.65 + 1e-8
    # Put the chassis into the floor: deleting non-foot collisions would fail this.
    mujoco.mj_resetData(model, data)
    data.qpos[2] = 0.001
    mujoco.mj_forward(model, data)
    assert any(
        any(model.geom(i).name == "chassis_collision" for i in c.geom)
        for c in data.contact
    )


def test_contract_and_task_agree():
    """Check on-disk provenance, cadence, actor layout, and PPO normalization."""
    contract = load_contract(ASSET)
    assert (
        hashlib.sha256((ASSET / "robot.xml").read_bytes()).hexdigest()
        == contract["model_sha256"]
    )
    for relative, digest in contract["source_hashes"].items():
        assert hashlib.sha256((PROJECT / relative).read_bytes()).hexdigest() == digest
    design = json.loads((PROJECT / "design.json").read_text())
    recipe = Path("/workspace/tools/cad/spider_sts3215.json")
    assert design == json.loads(recipe.read_text()), "Regenerate CAD after recipe edits"
    cfg = make_env_cfg(ASSET, 4)
    assert cfg.decimation * cfg.sim.mujoco.timestep == pytest.approx(0.02)
    assert tuple(cfg.observations["actor"].terms) == tuple(ACTOR_TERMS)
    assert sum(ACTOR_TERMS.values()) == 45
    assert "base_lin_vel" not in cfg.observations["actor"].terms
    assert cfg.commands["twist"].rel_standing_envs == 1
    assert cfg.rewards["posture"].weight < 0
    assert cfg.rewards["action_rate_l2"].weight < 0
    assert {"too_low", "fell_over", "body_contact"} <= cfg.terminations.keys()
    runner = make_rl_cfg()
    assert runner.actor.obs_normalization and runner.critic.obs_normalization
    assert runner.clip_actions == 1.0


def test_posture_penalty_increases_with_deviation():
    """Catch a reward sign reversal that would encourage joint-limit poses."""
    positions = torch.tensor([[0.0] * 12, [0.1] * 12])
    data = SimpleNamespace(
        joint_pos=positions, default_joint_pos=torch.zeros_like(positions)
    )
    env = SimpleNamespace(scene={"robot": SimpleNamespace(data=data)})
    penalty = make_env_cfg(ASSET, 4).rewards["posture"].weight * posture_cost(env)
    assert penalty[0] == 0 and penalty[1] < 0
