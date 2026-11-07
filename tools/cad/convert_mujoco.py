"""Convert the RobotCAD demo's URDF/Collada package to a portable MuJoCo scene."""

import json
import math
import os
import xml.etree.ElementTree as ET
from pathlib import Path

import collada
import mujoco
import numpy as np


def convert_mesh(source, destination):
    """Write transformed Collada triangles as OBJ in meters."""
    dae = collada.Collada(str(source))
    if dae.assetInfo.upaxis != "Z_UP" or dae.scene is None:
        raise ValueError(f"Expected a RobotCAD Z-up mesh scene: {source}")
    unit = dae.assetInfo.unitmeter or 1.0
    vertices = []
    offset = 1
    with destination.open("w") as stream:
        stream.write("# RobotCAD Collada conversion; coordinates in meters\n")
        for geometry in dae.scene.objects("geometry"):
            for primitive in geometry.primitives():
                if hasattr(primitive, "triangleset"):
                    primitive = primitive.triangleset()
                points = np.asarray(primitive.vertex, dtype=float) * unit
                faces = np.asarray(primitive.vertex_index)
                if faces.ndim != 2 or faces.shape[1] != 3:
                    raise ValueError(f"Expected triangle geometry: {source}")
                vertices.append(points)
                for point in points:
                    stream.write("v " + " ".join(f"{v:.12g}" for v in point) + "\n")
                for face in faces:
                    stream.write(
                        "f " + " ".join(str(int(i) + offset) for i in face) + "\n"
                    )
                offset += len(points)
    if not vertices:
        raise ValueError(f"Empty mesh: {source}")
    points = np.concatenate(vertices)
    return [points.min(axis=0).tolist(), points.max(axis=0).tolist()]


def main():
    """Generate MJCF and check its joint, effort limits, and position response."""
    state = Path(os.environ.get("JOSHUA_CAD_ROOT", "/workspace/output/robotcad"))
    project = Path(os.environ.get("JOSHUA_CAD_PROJECT", str(state / "project")))
    package = project / "robotcad_demo_description"
    output = project / "mujoco"
    meshes = output / "meshes"
    meshes.mkdir(parents=True, exist_ok=True)
    source = package / "urdf" / "robotcad_demo.urdf"
    tree = ET.parse(source)
    prefix = "package://robotcad_demo_description/"
    mesh_reports = {}
    for element in tree.findall(".//geometry/mesh"):
        uri = element.attrib["filename"]
        if not uri.startswith(prefix):
            raise ValueError(f"Unsupported mesh reference: {uri}")
        path = (package / uri[len(prefix) :]).resolve()
        if not path.is_relative_to(package.resolve()) or path.suffix.lower() != ".dae":
            raise ValueError(f"Expected a Collada mesh inside the demo package: {uri}")
        destination = meshes / (path.stem + ".obj")
        if destination.name not in mesh_reports:
            mesh_reports[destination.name] = convert_mesh(path, destination)
        element.set("filename", "meshes/" + destination.name)

    extension = ET.SubElement(tree.getroot(), "mujoco")
    ET.SubElement(
        extension,
        "compiler",
        {
            "meshdir": ".",
            "strippath": "false",
            "discardvisual": "false",
            "fusestatic": "false",
            "inertiafromgeom": "false",
        },
    )
    local_urdf = output / "robot_mujoco.urdf"
    ET.indent(tree, space="  ")
    tree.write(local_urdf, encoding="utf-8", xml_declaration=True)
    model = mujoco.MjModel.from_xml_path(str(local_urdf))
    robot_path = output / "robot.xml"
    mujoco.mj_saveLastXML(str(robot_path), model)

    robot_xml = ET.parse(robot_path)
    robot_xml.find("compiler").set("meshdir", "meshes")
    for asset in robot_xml.findall("asset/mesh"):
        asset.set("file", Path(asset.attrib["file"]).name)
    for body in robot_xml.findall(".//body"):
        for geom in body.findall("geom"):
            if geom.get("contype") == "0" and geom.get("conaffinity") == "0":
                color = (
                    "0.2 0.42 0.65 1"
                    if body.get("name") == "base_link"
                    else "0.95 0.56 0.18 1"
                )
                geom.set("rgba", color)
                geom.set("group", "1")
            else:
                geom.set("group", "3")
    ET.indent(robot_xml, space="  ")
    robot_xml.write(robot_path, encoding="utf-8", xml_declaration=True)

    scene = ET.parse(Path(__file__).with_name("mujoco_scene.xml"))
    actuators = ET.SubElement(scene.getroot(), "actuator")
    joint = tree.find("joint")
    if joint is None or joint.get("type") != "revolute":
        raise ValueError("This example converter expects one revolute joint")
    limit = joint.find("limit")
    lower, upper, effort = (float(limit.get(k)) for k in ("lower", "upper", "effort"))
    finite_limits = all(math.isfinite(v) for v in (lower, upper, effort))
    if not finite_limits or lower >= upper or effort <= 0:
        raise ValueError("Invalid joint position/effort limits")
    ET.SubElement(
        actuators,
        "position",
        {
            "name": joint.attrib["name"] + "_position",
            "joint": joint.attrib["name"],
            "kp": "0.1",
            "kv": "0.003",
            "ctrllimited": "true",
            "ctrlrange": f"{lower} {upper}",
            "forcelimited": "true",
            "forcerange": f"{-effort} {effort}",
        },
    )
    ET.indent(scene, space="  ")
    scene_path = output / "scene.xml"
    scene.write(scene_path, encoding="utf-8", xml_declaration=True)
    model = mujoco.MjModel.from_xml_path(str(scene_path))
    if (model.nq, model.nu) != (1, 1):
        raise ValueError("Expected one joint coordinate and one actuator")
    # MuJoCo's canonical XML writer rounds joint limits to six significant digits.
    np.testing.assert_allclose(model.jnt_range[0], [lower, upper], atol=5e-6)
    data = mujoco.MjData(model)
    mujoco.mj_forward(model, data)
    arm = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_BODY, "arm_link")
    np.testing.assert_allclose(data.xpos[arm], [0, 0, 0.024], atol=1e-9)
    response = []
    for fraction in (0.25, 0.75, 0.5):
        target = lower + fraction * (upper - lower)
        mujoco.mj_resetData(model, data)
        data.ctrl[0] = target
        peak_force = 0.0
        for _ in range(2000):
            mujoco.mj_step(model, data)
            if not np.isfinite(data.qpos).all() or not np.isfinite(data.qvel).all():
                raise ValueError("Non-finite simulation state")
            peak_force = max(peak_force, abs(float(data.actuator_force[0])))
        if abs(data.qpos[0] - target) > 0.02 or peak_force > effort + 1e-9:
            raise ValueError("Position response or actuator effort check failed")
        response.append(
            {
                "target_rad": target,
                "position_rad": float(data.qpos[0]),
                "peak_effort_Nm": peak_force,
            }
        )
    report = {
        "status": "ok",
        "mujoco_version": mujoco.__version__,
        "scene": str(scene_path),
        "mesh_bounds_m": mesh_reports,
        "joints": model.njnt,
        "actuators": model.nu,
        "fixed_base": True,
        "position_response": response,
    }
    (output / "validation.json").write_text(json.dumps(report, indent=2))
    print(
        json.dumps(
            {
                "status": "ok",
                "scene": str(scene_path),
                "joints": model.njnt,
                "actuators": model.nu,
            }
        )
    )


if __name__ == "__main__":
    main()
