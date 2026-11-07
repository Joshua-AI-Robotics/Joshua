"""Derive a mesh-free training model from the generated CAD model."""

import argparse
import hashlib
import json
import xml.etree.ElementTree as ET
from pathlib import Path

from .contract import JOINT_NAMES, LEGS, make_contract


def numbers(values):
    """Format SI vectors for MJCF."""
    return " ".join(f"{v:.12g}" for v in values)


def geom(body, name, shape, **attributes):
    """Add a primitive without changing the explicit CAD-derived inertia."""
    return ET.SubElement(
        body,
        "geom",
        name=name,
        type=shape,
        density="0",
        contype="2",
        conaffinity="1",
        group="3",
        **attributes,
    )


def prepare(project, output):
    """Preserve frames/inertias; replace render meshes by contact primitives."""
    design = json.loads((project / "design.json").read_text())
    manifest = json.loads((project / "manifest.json").read_text())
    source = project / "mujoco" / "robot.xml"
    root = ET.parse(source).getroot()
    if tuple(j.get("name") for j in root.iter("joint")) != JOINT_NAMES:
        raise ValueError("Unexpected CAD joint order; review the policy interface")
    root.remove(root.find("asset"))
    root.find("compiler").attrib = {"angle": "radian", "inertiafromgeom": "false"}
    for body in root.iter("body"):
        for item in list(body.findall("geom")):
            body.remove(item)
    base = root.find("worldbody/body")
    base.set("pos", "0 0 0")
    # Floor bit 1, robot bit 2: robot-ground collisions retained on every link.
    # Self collision is deferred; conservative primitive envelopes overlap at joints.
    body_cfg, leg = design["body"], design["leg"]
    plate = [v / 2000 for v in body_cfg["plate_mm"]]
    geom(base, "chassis_collision", "box", size=numbers(plate), rgba="0.14 0.34 0.47 1")
    # Include upper body envelope so a fallen robot cannot pass through its deck.
    geom(
        base,
        "deck_collision",
        "box",
        size="0.04 0.035 0.025",
        pos=numbers([0, 0, body_cfg["electronics_deck_z_mm"] / 1000]),
    )
    foot_names = []
    for name in LEGS:
        for segment, length, radius in (
            ("coxa", leg["coxa_mm"], 0.018),
            ("femur", leg["femur_mm"], 0.014),
            ("tibia", leg["tibia_mm"], 0.008),
        ):
            body = root.find(f".//body[@name='{name}_{segment}']")
            geom(
                body,
                f"{name}_{segment}_collision",
                "capsule",
                fromto=numbers([0, 0, 0, length / 1000, 0, 0]),
                size=str(radius),
                rgba="0.94 0.49 0.12 1",
            )
        foot = root.find(f".//body[@name='{name}_foot']")
        foot_names.append(f"{name}_foot_collision")
        geom(
            foot,
            foot_names[-1],
            "sphere",
            size=str(leg["foot_radius_mm"] / 1000),
            rgba="0.13 0.16 0.18 1",
        )
        ET.SubElement(foot, "site", name=f"{name}_foot", size="0.003")
    friction = numbers(design["simulation"]["ground_friction"])
    for item in root.iter("geom"):
        item.set("friction", friction)
        item.set("condim", "3")
        # The foot/shin clearance is only 2 mm. Select the robot's contact
        # parameters over the terrain default to avoid deep impact penetration.
        item.set("priority", "1")
        item.set("solref", "0.005 1")
    # mjlab creates the position actuators; model remains an entity, without floor.
    ET.indent(root, space="  ")
    model_bytes = ET.tostring(root, encoding="utf-8", xml_declaration=True)
    digest = hashlib.sha256(model_bytes).hexdigest()
    contract = make_contract(design, digest)
    contract["initial_base_z_m"] = manifest["initial_base_z_m"]
    contract["estimated_mass_kg"] = manifest["estimated_mass_kg"]
    contract["position_kp"] = design["simulation"]["position_kp"]
    contract["position_kv"] = design["simulation"]["position_kv"]
    contract["effort_limit_Nm"] = design["servo"]["simulation_effort_limit_Nm"]
    contract["feet"] = foot_names
    contract["source_hashes"] = {
        str(path.relative_to(project)): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in (source, project / "design.json", project / "manifest.json")
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "robot.xml").write_bytes(model_bytes)
    (output / "policy_contract.json").write_text(json.dumps(contract, indent=2) + "\n")
    print(f"Prepared {output}: 45 actor inputs, 12 actions, {digest[:12]}")


def main():
    """Generate assets under the ignored output tree."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    prepare(args.project, args.output)


if __name__ == "__main__":
    main()
