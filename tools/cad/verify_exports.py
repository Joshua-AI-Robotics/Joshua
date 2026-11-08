"""Check exported files independently of the CAD document."""

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

state = Path(os.environ.get("JOSHUA_CAD_ROOT", "/workspace/output/robotcad"))
root = Path(os.environ.get("JOSHUA_CAD_PROJECT", str(state / "project")))
package = root / "robotcad_demo_description"
urdf = package / "urdf" / "robotcad_demo.urdf"
model = URDF.from_xml_file(str(urdf))
assert model.get_root() == "base_link"
assert len(model.links) == 2 and len(model.joints) == 1
joint = model.joints[0]
assert joint.name == "shoulder_joint" and joint.type == "revolute"
assert joint.parent == "base_link" and joint.child == "arm_link"
np.testing.assert_allclose(joint.origin.xyz, [0, 0, 0.024])
np.testing.assert_allclose(joint.axis, [0, 0, 1])
np.testing.assert_allclose(
    [joint.limit.lower, joint.limit.upper], [-math.pi / 2, math.pi / 2]
)

report = {"status": "ok", "parts": {}, "links": {}, "mesh_files": {}}
for name, expected in [("base", [60, 50, 22]), ("arm", [118, 20, 6])]:
    path = root / "print" / f"{name}.stl"
    data = path.read_bytes()
    count = struct.unpack_from("<I", data, 80)[0]
    assert len(data) == 84 + count * 50
    dtype = np.dtype(
        [("normal", "<f4", (3,)), ("vertices", "<f4", (3, 3)), ("attr", "<u2")]
    )
    tris = np.frombuffer(data, dtype=dtype, offset=84)["vertices"].astype(float)
    dims = np.ptp(tris.reshape(-1, 3), axis=0)
    np.testing.assert_allclose(dims, expected, atol=0.02, rtol=0)
    edges = collections.Counter()
    for tri in np.round(tris, 6):
        points = [tuple(v) for v in tri]
        for a, b in [(0, 1), (1, 2), (2, 0)]:
            edges[tuple(sorted((points[a], points[b])))] += 1
    assert set(edges.values()) == {2}, (name, "non-manifold STL")
    volume = (
        np.einsum("ij,ij->i", tris[:, 0], np.cross(tris[:, 1], tris[:, 2])).sum() / 6
    )
    assert volume > 0
    report["parts"][name] = {
        "dimensions_mm": dims.tolist(),
        "facets": count,
        "closed_manifold": True,
        "volume_mm3": volume,
    }

expected_bounds = {
    "base_link": np.array([[-0.030, -0.025, 0], [0.030, 0.025, 0.022]]),
    "arm_link": np.array([[-0.010, -0.010, 0], [0.108, 0.010, 0.006]]),
}
tree = ET.parse(urdf)
for link in tree.findall("link"):
    name = link.attrib["name"]
    inertial = link.find("inertial")
    mass = float(inertial.find("mass").attrib["value"])
    assert mass > 0
    a = {k: float(v) for k, v in inertial.find("inertia").attrib.items()}
    inertia = np.array(
        [
            [a["ixx"], a["ixy"], a["ixz"]],
            [a["ixy"], a["iyy"], a["iyz"]],
            [a["ixz"], a["iyz"], a["izz"]],
        ]
    )
    eig = np.linalg.eigvalsh(inertia)
    assert np.all(eig > 0)
    assert eig[2] <= eig[0] + eig[1] + 1e-12
    report["links"][name] = {"mass_kg": mass, "inertia_eigenvalues": eig.tolist()}
    for role in ("visual", "collision"):
        element = link.find(role)
        mesh = element.find("geometry/mesh")
        prefix = "package://robotcad_demo_description/"
        assert mesh.attrib["filename"].startswith(prefix)
        path = package / mesh.attrib["filename"][len(prefix) :]
        assert path.is_file()
        dae = collada.Collada(str(path))
        arrays = []
        for geometry in dae.scene.objects("geometry"):
            for primitive in geometry.primitives():
                arrays.append(primitive.vertex)
        vertices = np.concatenate(arrays) * (dae.assetInfo.unitmeter or 1.0)
        if "scale" in mesh.attrib:
            vertices *= np.fromstring(mesh.attrib["scale"], sep=" ")
        origin = element.find("origin")
        if origin is not None:
            np.testing.assert_allclose(
                np.fromstring(origin.attrib.get("rpy", "0 0 0"), sep=" "), 0, atol=1e-9
            )
            vertices += np.fromstring(origin.attrib.get("xyz", "0 0 0"), sep=" ")
        bounds = np.array([vertices.min(axis=0), vertices.max(axis=0)])
        np.testing.assert_allclose(bounds, expected_bounds[name], atol=0.00002, rtol=0)
        report["mesh_files"][path.name] = {"role": role, "bounds_m": bounds.tolist()}

np.testing.assert_allclose(sum(v["mass_kg"] for v in report["links"].values()), 0.100)
(root / "export-validation.json").write_text(json.dumps(report, indent=2))
print(
    json.dumps(
        {
            "status": "ok",
            "links": len(model.links),
            "joints": len(model.joints),
            "closed_stls": len(report["parts"]),
            "resolved_meshes": len(report["mesh_files"]),
        }
    )
)
