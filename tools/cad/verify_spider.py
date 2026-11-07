"""Independently check the spider's manufacturing and robot-description exports."""

import collections
import json
import math
import os
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

import collada
import numpy as np
from urdf_parser_py.urdf import URDF


def origin_matrix(origin):
    """Convert a URDF fixed-axis RPY/XYZ origin to a homogeneous matrix."""
    result = np.eye(4)
    if origin is None:
        return result
    x, y, z = np.fromstring(origin.get("xyz", "0 0 0"), sep=" ")
    r, p, y_angle = np.fromstring(origin.get("rpy", "0 0 0"), sep=" ")
    cr, sr, cp, sp, cy, sy = (
        math.cos(r),
        math.sin(r),
        math.cos(p),
        math.sin(p),
        math.cos(y_angle),
        math.sin(y_angle),
    )
    result[:3, :3] = [
        [cy * cp, cy * sp * sr - sy * cr, cy * sp * cr + sy * sr],
        [sy * cp, sy * sp * sr + cy * cr, sy * sp * cr - cy * sr],
        [-sp, cp * sr, cp * cr],
    ]
    result[:3, 3] = [x, y, z]
    return result


def mesh_vertices(path):
    """Read scene-transformed Collada vertices in meters."""
    dae = collada.Collada(str(path))
    arrays = [p.vertex for g in dae.scene.objects("geometry") for p in g.primitives()]
    return np.concatenate(arrays) * (dae.assetInfo.unitmeter or 1)


def main():
    """Validate real exported surfaces, graph, link frames, and inertia."""
    project = Path(os.environ["JOSHUA_CAD_PROJECT"])
    manifest = json.loads((project / "manifest.json").read_text())
    cfg = json.loads((project / "design.json").read_text())
    package = project / (cfg["name"] + "_description")
    urdf = package / "urdf" / (cfg["name"] + ".urdf")
    parsed = URDF.from_xml_file(str(urdf))
    tree = ET.parse(urdf)
    assert parsed.get_root() == "base_link" and len(parsed.links) == 17
    assert len([j for j in parsed.joints if j.type == "revolute"]) == 12
    assert len([j for j in parsed.joints if j.type == "fixed"]) == 4
    assert not manifest["neutral_case_interferences"]
    transforms = {"base_link": np.eye(4)}
    pending = list(tree.findall("joint"))
    while pending:
        advanced = False
        for joint in pending[:]:
            parent = joint.find("parent").get("link")
            child = joint.find("child").get("link")
            if parent not in transforms:
                continue
            assert child not in transforms, "Cycle or multiple parents"
            transforms[child] = transforms[parent] @ origin_matrix(joint.find("origin"))
            pending.remove(joint)
            advanced = True
            if joint.get("type") == "revolute":
                np.testing.assert_allclose(
                    np.fromstring(joint.find("axis").get("xyz"), sep=" "), [0, 0, 1]
                )
                limit = joint.find("limit")
                assert float(limit.get("lower")) < 0 < float(limit.get("upper"))
                assert (
                    float(limit.get("effort"))
                    == cfg["servo"]["simulation_effort_limit_Nm"]
                )
        assert advanced, "Disconnected kinematic graph"
    assert len(transforms) == 17
    for name, metadata in manifest["links_metadata"].items():
        np.testing.assert_allclose(
            transforms[name][:3, 3],
            np.array(metadata["placement_position_mm"]) / 1000,
            atol=5e-7,
        )
    part_reports = {}
    for name, metadata in manifest["print_parts"].items():
        data = (project / "print" / (name + ".stl")).read_bytes()
        count = struct.unpack_from("<I", data, 80)[0]
        assert len(data) == 84 + 50 * count
        dtype = np.dtype(
            [("normal", "<f4", (3,)), ("vertices", "<f4", (3, 3)), ("attr", "<u2")]
        )
        tris = np.frombuffer(data, dtype=dtype, offset=84)["vertices"].astype(float)
        vertices = tris.reshape(-1, 3)
        dims = np.ptp(vertices, axis=0)
        np.testing.assert_allclose(dims, metadata["dimensions_mm"], atol=0.0001)
        assert np.all(dims[:2] <= np.array(cfg["printer_bed_mm"]) + 0.001)
        assert vertices.min() >= -0.0001
        edges = collections.Counter()
        for triangle in np.round(tris, 5):
            points = [tuple(v) for v in triangle]
            for a, b in [(0, 1), (1, 2), (2, 0)]:
                edges[tuple(sorted((points[a], points[b])))] += 1
        assert set(edges.values()) == {2}, (name, "non-manifold edges")
        volume = (
            np.einsum("ij,ij->i", tris[:, 0], np.cross(tris[:, 1], tris[:, 2])).sum()
            / 6
        )
        assert volume > 0
        np.testing.assert_allclose(volume, metadata["volume_mm3"], rtol=0.015)
        part_reports[name] = {
            "closed_manifold": True,
            "dimensions_mm": dims.tolist(),
            "facets": count,
        }
    world_points = []
    seen_meshes = set()
    motor_count = 0
    total_mass = 0
    for link in tree.findall("link"):
        name = link.get("name")
        inertial = link.find("inertial")
        mass = float(inertial.find("mass").get("value"))
        assert mass > 0
        total_mass += mass
        a = {k: float(v) for k, v in inertial.find("inertia").attrib.items()}
        matrix = np.array(
            [
                [a["ixx"], a["ixy"], a["ixz"]],
                [a["ixy"], a["iyy"], a["iyz"]],
                [a["ixz"], a["iyz"], a["izz"]],
            ]
        )
        eig = np.linalg.eigvalsh(matrix)
        assert eig.min() > 0 and eig.max() <= eig.sum() - eig.max() + 1e-12
        for visual in link.findall("visual"):
            motor_count += visual.get("name", "").endswith("_servo")
            mesh = visual.find("geometry/mesh")
            prefix = "package://" + package.name + "/"
            assert mesh.get("filename").startswith(prefix)
            path = package / mesh.get("filename")[len(prefix) :]
            assert path.is_file()
            seen_meshes.add(path.name)
            vertices = mesh_vertices(path)
            if "scale" in mesh.attrib:
                vertices *= np.fromstring(mesh.get("scale"), sep=" ")
            matrix = transforms[name] @ origin_matrix(visual.find("origin"))
            world_points.append(vertices @ matrix[:3, :3].T + matrix[:3, 3])
        for mesh in link.findall("collision/geometry/mesh"):
            assert (
                package / mesh.get("filename").split(package.name + "/", 1)[1]
            ).is_file()
    assert motor_count == 12
    vertices = np.concatenate(world_points)
    bounds = np.array([vertices.min(axis=0), vertices.max(axis=0)])
    np.testing.assert_allclose(
        bounds, np.array(manifest["body_frame_bounds_mm"]) / 1000, atol=0.0003
    )
    np.testing.assert_allclose(total_mass, manifest["estimated_mass_kg"])
    report = {
        "status": "ok",
        "printed_part_types": len(part_reports),
        "print_parts": part_reports,
        "motor_envelopes": motor_count,
        "links": 17,
        "actuated_joints": 12,
        "resolved_visual_meshes": len(seen_meshes),
        "assembled_bounds_m": bounds.tolist(),
        "mass_kg_estimate": total_mass,
        "neutral_case_interferences": 0,
        "limitation": (
            "Only nominal motor-case clearance at the neutral CAD pose is "
            "checked. Full motion, cable, hardware and real-part fit require "
            "review."
        ),
    }
    (project / "export-validation.json").write_text(json.dumps(report, indent=2))
    print(
        json.dumps(
            {
                k: v
                for k, v in report.items()
                if k not in ("print_parts", "assembled_bounds_m")
            }
        )
    )


if __name__ == "__main__":
    main()
