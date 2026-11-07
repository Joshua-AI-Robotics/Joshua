"""Convert the spider to floating-base and suspended MuJoCo inspection scenes."""

import json
import os
import xml.etree.ElementTree as ET
from pathlib import Path

import mujoco
import numpy as np
from convert_mujoco import convert_mesh


def write_xml(tree, path):
    """Write consistently formatted XML."""
    ET.indent(tree, space="  ")
    tree.write(path, encoding="utf-8", xml_declaration=True)


def make_scene(cfg, joints, robot_file, output):
    """Add terrain, lighting, and explicitly bounded position drives."""
    root = ET.Element("mujoco", {"model": cfg["name"]})
    ET.SubElement(root, "include", {"file": robot_file})
    sim = cfg["simulation"]
    ET.SubElement(
        root,
        "option",
        {
            "timestep": str(sim["timestep_s"]),
            "integrator": "implicitfast",
            "gravity": "0 0 -9.81",
        },
    )
    ET.SubElement(root, "statistic", {"center": "0 0 0.09", "extent": "0.65"})
    visual = ET.SubElement(root, "visual")
    ET.SubElement(
        visual, "headlight", {"ambient": "0.35 0.35 0.35", "diffuse": "0.55 0.55 0.55"}
    )
    ET.SubElement(
        visual,
        "global",
        {"azimuth": "130", "elevation": "-25", "offwidth": "1200", "offheight": "900"},
    )
    world = ET.SubElement(root, "worldbody")
    ET.SubElement(
        world,
        "light",
        {"pos": "0 0 1", "dir": "0 0 -1", "directional": "true", "castshadow": "false"},
    )
    ET.SubElement(
        world,
        "geom",
        {
            "name": "floor",
            "type": "plane",
            "size": "1 1 0.02",
            "rgba": "0.25 0.29 0.34 1",
            "friction": " ".join(str(v) for v in sim["ground_friction"]),
        },
    )
    ET.SubElement(
        world,
        "camera",
        {
            "name": "front",
            "pos": "0.6 -0.7 0.5",
            "xyaxes": "0.7592566 0.6507914 0 -0.282 0.329 0.901",
        },
    )
    actuator = ET.SubElement(root, "actuator")
    for joint in joints:
        limit = joint.find("limit")
        lower, upper, effort = (
            float(limit.get(k)) for k in ("lower", "upper", "effort")
        )
        ET.SubElement(
            actuator,
            "position",
            {
                "name": joint.get("name") + "_position",
                "joint": joint.get("name"),
                "kp": str(sim["position_kp"]),
                "kv": str(sim["position_kv"]),
                "ctrllimited": "true",
                "ctrlrange": f"{lower} {upper}",
                "forcelimited": "true",
                "forcerange": f"{-effort} {effort}",
            },
        )
    write_xml(ET.ElementTree(root), output)


def check_frames(model, manifest, height):
    """Compare MuJoCo's compiled neutral link frames with the saved CAD frames."""
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)
    for name, expected in manifest["links_metadata"].items():
        body = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_BODY, name)
        assert body >= 0, name
        xyz = np.array(expected["placement_position_mm"]) / 1000 + [0, 0, height]
        np.testing.assert_allclose(data.xpos[body], xyz, atol=2e-6)
        x, y, z, w = expected["placement_quaternion_xyzw"]
        rotation = np.empty(9)
        mujoco.mju_quat2Mat(rotation, np.array([w, x, y, z]))
        np.testing.assert_allclose(data.xmat[body], rotation, atol=1e-5)
    return data


def main():
    """Generate scenes and check articulation, standing, and torque assumptions."""
    project = Path(os.environ["JOSHUA_CAD_PROJECT"])
    cfg = json.loads((project / "design.json").read_text())
    manifest = json.loads((project / "manifest.json").read_text())
    package = project / (cfg["name"] + "_description")
    output = project / "mujoco"
    meshes = output / "meshes"
    meshes.mkdir(parents=True, exist_ok=True)
    tree = ET.parse(package / "urdf" / (cfg["name"] + ".urdf"))
    joints = [j for j in tree.findall("joint") if j.get("type") == "revolute"]
    assert len(joints) == 12
    prefix = "package://" + package.name + "/"
    reports = {}
    for mesh in tree.findall(".//geometry/mesh"):
        uri = mesh.get("filename")
        assert uri.startswith(prefix)
        source = (package / uri[len(prefix) :]).resolve()
        assert source.is_relative_to(package.resolve())
        name = source.stem + ".obj"
        if name not in reports:
            reports[name] = convert_mesh(source, meshes / name)
        mesh.set("filename", "meshes/" + name)
    ext = ET.SubElement(tree.getroot(), "mujoco")
    ET.SubElement(
        ext,
        "compiler",
        {
            "meshdir": ".",
            "strippath": "false",
            "discardvisual": "false",
            "fusestatic": "false",
            "inertiafromgeom": "false",
        },
    )
    local_urdf = output / "spider_mujoco.urdf"
    write_xml(tree, local_urdf)
    model = mujoco.MjModel.from_xml_path(str(local_urdf))
    canonical = output / "robot_fixed.xml"
    mujoco.mj_saveLastXML(str(canonical), model)
    robot = ET.parse(canonical)
    robot.find("compiler").set("meshdir", "meshes")
    for mesh in robot.findall("asset/mesh"):
        mesh.set("file", Path(mesh.get("file")).name)
    colors = {
        v.get("name"): v.find("material/color").get("rgba")
        for v in tree.findall("link/visual")
    }
    for geom in robot.findall(".//geom"):
        if geom.get("contype") == "0" and geom.get("conaffinity") == "0":
            geom.set("group", "1")
            if geom.get("name") in colors:
                geom.set("rgba", colors[geom.get("name")])
        else:
            geom.set("group", "3")
            geom.set(
                "friction",
                " ".join(str(v) for v in cfg["simulation"]["ground_friction"]),
            )
    base = robot.find("worldbody/body[@name='base_link']")
    fixture_height = max(0.28, manifest["initial_base_z_m"] + 0.15)
    base.set("pos", f"0 0 {fixture_height}")
    write_xml(robot, canonical)
    make_scene(cfg, joints, "robot_fixed.xml", output / "inspection.xml")
    base.set("pos", f"0 0 {manifest['initial_base_z_m']}")
    ET.SubElement(base, "freejoint", {"name": "floating_base"})
    write_xml(robot, output / "robot.xml")
    make_scene(cfg, joints, "robot.xml", output / "scene.xml")

    fixed = mujoco.MjModel.from_xml_path(str(output / "inspection.xml"))
    assert fixed.nq == 12 and fixed.nu == 12
    check_frames(fixed, manifest, fixture_height)
    fixture_response = []
    for target in (-0.12, 0.12, 0):
        data = mujoco.MjData(fixed)
        data.ctrl[:] = target
        for _ in range(2000):
            mujoco.mj_step(fixed, data)
            assert np.isfinite(data.qpos).all() and np.isfinite(data.qvel).all()
            assert (
                np.max(np.abs(data.actuator_force))
                <= cfg["servo"]["simulation_effort_limit_Nm"] + 1e-8
            )
        error = float(np.max(np.abs(data.qpos - target)))
        assert error < 0.08, ("fixture response", error, data.qpos.tolist())
        fixture_response.append({"target_rad": target, "max_error_rad": error})
    floating = mujoco.MjModel.from_xml_path(str(output / "scene.xml"))
    assert (floating.nq, floating.nv, floating.nu) == (19, 18, 12)
    data = check_frames(floating, manifest, manifest["initial_base_z_m"])
    total_mass = float(np.sum(floating.body_mass))
    np.testing.assert_allclose(total_mass, manifest["estimated_mass_kg"], rtol=2e-5)
    external = np.zeros(floating.nv)
    for name in manifest["feet_in_body_frame_mm"]:
        body = mujoco.mj_name2id(floating, mujoco.mjtObj.mjOBJ_BODY, name)
        point = data.xpos[body].copy()
        point[2] -= cfg["leg"]["foot_radius_mm"] / 1000
        jp, jr = np.zeros((3, floating.nv)), np.zeros((3, floating.nv))
        mujoco.mj_jac(floating, data, jp, jr, point, body)
        external += jp.T @ np.array([0, 0, total_mass * 9.81 / 4])
    static_torque = (data.qfrc_bias - external)[6:]
    assert np.max(np.abs(static_torque)) < cfg["servo"]["simulation_effort_limit_Nm"]
    for _ in range(5000):
        mujoco.mj_step(floating, data)
        assert np.isfinite(data.qpos).all() and np.isfinite(data.qvel).all()
        assert (
            np.max(np.abs(data.actuator_force))
            <= cfg["servo"]["simulation_effort_limit_Nm"] + 1e-8
        )
    base_id = mujoco.mj_name2id(floating, mujoco.mjtObj.mjOBJ_BODY, "base_link")
    upright = float(data.xmat[base_id].reshape(3, 3)[2, 2])
    standing = bool(data.xpos[base_id, 2] > 0.04 and upright > 0.9)
    report = {
        "status": "ok" if standing else "settling_review_needed",
        "actuators": 12,
        "links": 17,
        "floating_base_nq_nv": [floating.nq, floating.nv],
        "mujoco_version": mujoco.__version__,
        "mass_kg_estimate": total_mass,
        "converted_meshes": len(reports),
        "fixture_response": fixture_response,
        "standing_5s": standing,
        "settled_base_height_m": float(data.xpos[base_id, 2]),
        "upright_cosine": upright,
        "contacts": int(data.ncon),
        "static_equal_four_foot_support_torques_Nm": static_torque.tolist(),
        "static_support_limitation": (
            "Equal four-foot vertical support at neutral pose, estimated "
            "masses; no walking or dynamic load qualification."
        ),
        "mesh_bounds_m": reports,
    }
    (output / "validation.json").write_text(json.dumps(report, indent=2))
    if not standing:
        raise ValueError(f"Free-base settling needs review: {report}")
    try:
        from PIL import Image

        with mujoco.Renderer(floating, height=900, width=1200) as renderer:
            neutral = mujoco.MjData(floating)
            mujoco.mj_forward(floating, neutral)
            renderer.update_scene(neutral, camera="front")
            Image.fromarray(renderer.render()).save(output / "preview.png")
    except ImportError:
        pass
    print(json.dumps({k: v for k, v in report.items() if k != "mesh_bounds_m"}))


if __name__ == "__main__":
    main()
