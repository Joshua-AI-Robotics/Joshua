"""Build the four-leg STS3215 prototype in FreeCAD and export its assets."""

import csv
import json
import math
import shutil
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

import FreeCAD as App
import FreeCADGui as Gui
import MeshPart
import numpy as np
import Part
from freecad.cross.joint_proxy import make_joint
from freecad.cross.link_proxy import make_link
from freecad.cross.robot_proxy import make_robot

TOOLS = Path(__file__).resolve().parent


def transform(shape, placement):
    """Return a transformed copy, preserving the original manufacturing shape."""
    result = shape.copy()
    result.transformShape(placement.toMatrix(), True)
    return result


def place(x=0, y=0, z=0, rotation=None):
    """Make a placement in millimeters."""
    return App.Placement(App.Vector(x, y, z), rotation or App.Rotation())


def box(dx, dy, dz, x=0, y=0, z=0):
    """Create a box with its offset baked into its geometry."""
    return Part.makeBox(dx, dy, dz, App.Vector(x, y, z))


def cylinder(radius, height, x=0, y=0, z=0, direction=(0, 0, 1)):
    """Create a cylinder with a specified base and direction."""
    return Part.makeCylinder(
        radius, height, App.Vector(x, y, z), App.Vector(*direction)
    )


def union(shapes):
    """Fuse connected shapes and remove redundant Boolean boundaries."""
    return (
        shapes[0].multiFuse(shapes[1:]).removeSplitter()
        if len(shapes) > 1
        else shapes[0]
    )


def beam(a, b, width, z, thickness):
    """A rounded planar structural rib."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy)
    middle = box(length, width, thickness, 0, -width / 2, z)
    middle = transform(
        middle,
        place(
            a[0],
            a[1],
            rotation=App.Rotation(
                App.Vector(0, 0, 1), math.degrees(math.atan2(dy, dx))
            ),
        ),
    )
    return union(
        [
            middle,
            cylinder(width / 2, thickness, *a, z),
            cylinder(width / 2, thickness, *b, z),
        ]
    )


class SpiderBuilder:
    """Geometry and rigid-body bookkeeping for the prototype."""

    def __init__(self, project):
        self.project = project
        self.cfg = json.loads((TOOLS / "spider_sts3215.json").read_text())
        self.s = self.cfg["servo"]
        self.leg = self.cfg["leg"]
        self.body = self.cfg["body"]
        self.doc = App.newDocument("Spider_STS3215")
        self.sources = self.doc.addObject(
            "App::DocumentObjectGroup", "Manufacturing_and_Hardware"
        )
        self.robot = make_robot(self.cfg["name"], self.doc)
        self.robot.OutputPath = str(project / (self.cfg["name"] + "_description"))
        self.robot.addProperty("App::PropertyString", "PrototypeRevision", "Design")
        self.robot.PrototypeRevision = self.cfg["revision"]
        self.robot.addProperty("App::PropertyString", "MotorReference", "Design")
        self.robot.MotorReference = self.s["qualification"]
        self.links = {}
        self.joints = []
        self.components = {}
        self.prints = {}
        self.print_reports = {}
        self.rz = lambda a: App.Rotation(App.Vector(0, 0, 1), a)
        self.rx = lambda a: App.Rotation(App.Vector(1, 0, 0), a)
        self.mounts = [(-16, -19), (-16, 19), (36, -19), (36, 19)]

    def feature(self, name, shape, color):
        obj = self.doc.addObject("Part::Feature", name)
        obj.Shape = shape
        obj.ViewObject.ShapeColor = color
        obj.ViewObject.LineColor = (0.12, 0.14, 0.17)
        self.sources.addObject(obj)
        return obj

    def printed(self, name, shape, quantity, material="PETG", rotation=None, notes=""):
        if not shape.isValid() or len(shape.Solids) != 1:
            raise ValueError(f"Invalid or disconnected printable part: {name}")
        self.prints[name] = {
            "shape": shape,
            "quantity": quantity,
            "material": material,
            "rotation": rotation or App.Rotation(),
            "notes": notes,
        }
        return shape

    def component(self, link, name, shape, color, material="PETG", mass=None):
        if len(shape.Solids) != 1:
            raise ValueError(f"Component must contain one solid: {name}")
        shape = shape.Solids[0]
        obj = self.feature(name, shape, color)
        if mass is None:
            p = self.cfg["print"]
            density = (
                p["foot_density_kg_mm3"] if material == "TPU" else p["density_kg_mm3"]
            )
            fraction = 0.95 if material == "TPU" else p["effective_solid_fraction"]
            mass = shape.Volume * density * fraction
        self.components[link].append(
            {
                "object": obj,
                "shape": shape,
                "mass_kg": mass,
                "material": material,
                "color": color,
            }
        )
        return obj

    def link(self, name):
        result = make_link(name, self.doc)
        self.robot.addObject(result)
        result.MaterialNotCalculate = True
        self.links[name] = result
        self.components[name] = []
        return result

    def joint(self, name, parent, child, origin, kind="revolute", limits=None):
        result = make_joint(name, self.doc, robot=self.robot)
        result.Type = kind
        result.Parent, result.Child = parent, child
        result.Origin = origin
        result.ViewObject.ShowAxis = False
        if limits:
            result.LowerLimit, result.UpperLimit = limits
            result.Effort = self.s["simulation_effort_limit_Nm"]
            result.Velocity = math.degrees(self.s["simulation_velocity_limit_rad_s"])
        self.joints.append(result)
        self.doc.recompute()
        return result

    def horn_plate(self):
        shape = cylinder(18, 4, z=self.s["printed_horn_plane_mm"])
        return self.drill_horn(shape)

    def drill_horn(self, shape):
        """Keep the horn screw and center holes clear after rib fusion."""
        shape = shape.cut(cylinder(3.75, 10, z=3))
        for a in range(0, 360, 90):
            x = self.s["horn_pcd_mm"] / 2 * math.cos(math.radians(a))
            y = self.s["horn_pcd_mm"] / 2 * math.sin(math.radians(a))
            shape = shape.cut(
                cylinder(self.s["horn_screw_clearance_mm"] / 2, 10, x, y, 3)
            )
        return shape.removeSplitter()

    def mount_plate(self):
        shape = box(60, 44, 4, -20, -22, -43)
        shape = shape.cut(box(20, 14, 8, 5, -7, -45))
        for x, y in self.mounts:
            shape = shape.cut(cylinder(1.7, 8, x, y, -45))
        return shape.removeSplitter()

    def cradle(self):
        s = self.s
        front = -s["shaft_from_front_mm"] - s["case_clearance_each_side_mm"]
        length = s["case_mm"][0] + 2 * s["case_clearance_each_side_mm"]
        half_w = s["case_mm"][1] / 2 + s["case_clearance_each_side_mm"]
        shapes = [box(60, 44, 4, -20, -22, -39)]
        for sign in (-1, 1):
            y = half_w if sign == 1 else -half_w - 4
            shapes.append(box(length, 4, 12, front, y, -35))
            shapes.append(cylinder(4.3, 35.35, 26, sign * 17, -35))
        shape = union(shapes).cut(box(20, 14, 8, 5, -7, -41))
        for x, y in self.mounts:
            shape = shape.cut(cylinder(1.7, 8, x, y, -41))
            shape = shape.cut(cylinder(3.2, 38, x, y, -35))
        for y in (-17, 17):
            shape = shape.cut(cylinder(2.05, 6.2, 26, y, -5.8))
        return shape.removeSplitter()

    def clamp(self):
        shape = box(8, 44, 3.5, 22, -22, 0.35)
        for y in (-17, 17):
            shape = shape.cut(cylinder(1.7, 6, 26, y, -1))
        return shape.removeSplitter()

    def servo_proxy(self):
        length, width, height = self.s["case_mm"]
        shape = box(
            length, width, height, -self.s["shaft_from_front_mm"], -width / 2, -height
        )
        return union(
            [
                shape,
                cylinder(self.s["shaft_diameter_mm"] / 2, self.s["shaft_height_mm"]),
            ]
        )

    def metal_horn(self):
        shape = cylinder(self.s["horn_diameter_mm"] / 2, 2.5, z=3.5)
        shape = shape.cut(cylinder(1.6, 5, z=3))
        for a in range(0, 360, 90):
            x = 7 * math.cos(math.radians(a))
            y = 7 * math.sin(math.radians(a))
            shape = shape.cut(cylinder(1.5, 5, x, y, 3))
        return shape.removeSplitter()

    def add_motor(self, link, prefix, placement):
        for name, shape, color, material, mass in [
            (
                "servo",
                self.servo_proxy(),
                (0.16, 0.17, 0.2),
                "purchased_servo",
                self.s["mass_kg"],
            ),
            (
                "cradle",
                self.prints["servo_cradle"]["shape"],
                (0.20, 0.39, 0.48),
                "PETG",
                None,
            ),
            (
                "clamp",
                self.prints["servo_clamp"]["shape"],
                (0.32, 0.52, 0.61),
                "PETG",
                None,
            ),
        ]:
            self.component(
                link,
                prefix + "_" + name,
                transform(shape, placement),
                color,
                material,
                mass,
            )

    def complete_link(self, name, collisions):
        link = self.links[name]
        components = self.components[name]
        link.Real = [c["object"] for c in components]
        link.Visual = list(link.Real)
        link.Collision = [
            self.feature(name + f"_collision_{i}", shape, (0.6, 0.3, 0.3))
            for i, shape in enumerate(collisions)
        ]
        total = sum(c["mass_kg"] for c in components)
        center = (
            sum(
                (
                    c["mass_kg"] * np.array(list(c["shape"].CenterOfMass))
                    for c in components
                ),
                np.zeros(3),
            )
            / total
        )
        inertia = np.zeros((3, 3))
        for c in components:
            shape, mass = c["shape"], c["mass_kg"]
            m = shape.MatrixOfInertia
            own = np.array(
                [[m.A11, m.A12, m.A13], [m.A21, m.A22, m.A23], [m.A31, m.A32, m.A33]]
            )
            r = (np.array(list(shape.CenterOfMass)) - center) / 1000
            inertia += own * mass / shape.Volume / 1e6 + mass * (
                r.dot(r) * np.eye(3) - np.outer(r, r)
            )
        link.Mass = f"{total} kg"
        link.CenterOfMass = place(*center)
        link.Ixx, link.Ixy, link.Ixz = inertia[0]
        link.Iyy, link.Iyz = inertia[1, 1:]
        link.Izz = inertia[2, 2]
        eig = np.linalg.eigvalsh(inertia)
        if min(eig) <= 0 or max(eig) > sum(eig) - max(eig) + 1e-12:
            raise ValueError(f"Invalid composite inertia: {name}")

    def make_prints(self):
        self.printed(
            "servo_cradle",
            self.cradle(),
            12,
            notes=(
                "Flat base on bed; M3 heat-set insert pilots in the two tall "
                "posts. Open end and cable window remain clear."
            ),
        )
        self.printed(
            "servo_clamp",
            self.clamp(),
            12,
            notes=(
                "Flat face on bed. Use thin rubber liner; do not crush the servo "
                "case."
            ),
        )
        hp = self.horn_plate()
        pitch_frame = place(
            self.leg["coxa_mm"], 0, self.leg["pitch_axis_rise_mm"], self.rx(-90)
        )
        coxa = union(
            [
                hp,
                beam((0, 0), (15, -45), 12, 6, 4),
                beam((15, -45), (self.leg["coxa_mm"] - 10, -45), 12, 6, 4),
                transform(self.mount_plate(), pitch_frame),
            ]
        )
        self.printed(
            "coxa_carrier",
            self.drill_horn(coxa),
            4,
            rotation=self.rx(90),
            notes=(
                "Side down; supports under offset flanges may be needed. Shares "
                "the same part on all four legs."
            ),
        )
        knee_frame = place(self.leg["femur_mm"], 0, 0, self.rz(180))
        drop = self.leg["femur_mm"] - 48
        femur = union(
            [
                hp,
                beam((0, 0), (drop + 2, 0), 20, 6, 4),
                box(4, 20, 53, drop, -10, -43),
                box(12, 20, 4, drop, -10, -43),
                transform(self.mount_plate(), knee_frame),
            ]
        )
        self.printed(
            "femur_carrier",
            self.drill_horn(femur),
            4,
            rotation=self.rx(90),
            notes=(
                "Print on side; use supports where the offset mounting flange "
                "overhangs. Preserve cable window."
            ),
        )
        length = self.leg["tibia_mm"]
        tibia = union([hp, beam((0, 0), (length, 0), 14, 6, 4)])
        tibia = tibia.cut(box(20, 100, 100, length, -50, -50))
        tibia = union(
            [
                tibia,
                cylinder(
                    self.leg["foot_spigot_radius_mm"],
                    self.leg["foot_spigot_length_mm"],
                    length,
                    0,
                    8,
                    (1, 0, 0),
                ),
            ]
        )
        self.printed(
            "tibia",
            self.drill_horn(tibia),
            4,
            notes=(
                "Broad flat beam face on bed. Horn adapter is integral; toe "
                "accepts the TPU cap."
            ),
        )
        radius = self.leg["foot_radius_mm"]
        foot = Part.makeSphere(radius).common(
            box(radius + 1, 2 * radius + 2, 2 * radius + 2, 0, -radius - 1, -radius - 1)
        )
        foot = foot.cut(
            cylinder(
                self.leg["foot_spigot_radius_mm"]
                - self.leg["foot_interference_radial_mm"],
                self.leg["foot_spigot_length_mm"] + 0.3,
                -0.1,
                0,
                0,
                (1, 0, 0),
            )
        ).removeSplitter()
        self.printed(
            "foot_cap",
            foot,
            4,
            material="TPU",
            rotation=App.Rotation(App.Vector(0, 1, 0), -90),
            notes=(
                "Flat annulus on bed; inspect bridging above the socket. Test "
                "interference and retention on one toe first."
            ),
        )
        body_x, body_y, height = self.body["plate_mm"]
        chassis = box(body_x, body_y, height, -body_x / 2, -body_y / 2)
        ax, ay = self.body["hip_axis_xy_mm"]
        for sx, sy in [(1, 1), (1, -1), (-1, 1), (-1, -1)]:
            phi = math.degrees(math.atan2(sy * ay, sx * ax))
            frame = place(
                sx * ax, sy * ay, self.body["hip_axis_z_mm"], self.rz(phi + 180)
            )
            chassis = chassis.cut(transform(box(20, 14, 8, 5, -7, -45), frame))
            for x, y in self.mounts:
                chassis = chassis.cut(transform(cylinder(1.7, 9, x, y, -45), frame))
        for x, y in self.body["standoff_xy_mm"]:
            chassis = chassis.cut(cylinder(1.7, 8, x, y, -1))
        for x in (-16, 16):
            for y in (-12, 12):
                chassis = chassis.cut(cylinder(6, 8, x, y, -1))
        self.printed(
            "chassis",
            chassis.removeSplitter(),
            1,
            notes=(
                "Flat on bed; 170 x 160 mm. A 5 mm brim fits 180 x 180 mm "
                "nominally; check the printer's actual usable area."
            ),
        )
        dx, dy, dz = self.body["electronics_deck_mm"]
        deck = box(dx, dy, dz, -dx / 2, -dy / 2)
        for x, y in self.body["standoff_xy_mm"]:
            deck = deck.cut(cylinder(1.7, 6, x, y, -1))
        for x in (-15, 15):
            for y in (-20, 20):
                deck = deck.cut(box(10, 3.5, 6, x - 5, y - 1.75, -1))
        self.printed(
            "electronics_deck",
            deck.removeSplitter(),
            1,
            notes=(
                "Flat on bed; slots accept small hook-and-loop straps. Payload "
                "envelope is illustrative."
            ),
        )
        post_height = self.body["electronics_deck_z_mm"] - height
        post = (
            cylinder(4.5, post_height)
            .cut(cylinder(1.7, post_height + 2, z=-1))
            .removeSplitter()
        )
        self.printed(
            "deck_standoff",
            post,
            4,
            rotation=self.rx(90),
            notes=(
                "Through-bore for M3 threaded rod. Print on side with supports or "
                "vertically with a brim."
            ),
        )
        gauge = box(60, 40, 4, -20, -20).cut(
            box(
                self.s["case_mm"][0] + 0.7,
                self.s["case_mm"][1] + 0.7,
                6,
                -self.s["shaft_from_front_mm"] - 0.35,
                -self.s["case_mm"][1] / 2 - 0.35,
                -1,
            )
        )
        self.printed(
            "servo_fit_gauge",
            gauge.removeSplitter(),
            1,
            notes=(
                "Optional first print. Checks footprint clearance only, not cable "
                "plugs or horn stack."
            ),
        )
        self.printed(
            "horn_fit_coupon",
            hp,
            1,
            notes=(
                "Optional first print. Check four M3 holes on 14 mm PCD and "
                "central screw access against your actual supplied horn."
            ),
        )

    def make_assembly(self):
        blue, orange, gray = (0.14, 0.34, 0.47), (0.94, 0.49, 0.12), (0.64, 0.67, 0.7)
        self.link("base_link")
        self.component("base_link", "chassis", self.prints["chassis"]["shape"], blue)
        self.component(
            "base_link",
            "electronics_deck",
            transform(
                self.prints["electronics_deck"]["shape"],
                place(z=self.body["electronics_deck_z_mm"]),
            ),
            blue,
        )
        for i, (x, y) in enumerate(self.body["standoff_xy_mm"]):
            self.component(
                "base_link",
                f"deck_standoff_{i}",
                transform(
                    self.prints["deck_standoff"]["shape"],
                    place(x, y, self.body["plate_mm"][2]),
                ),
                gray,
            )
        dx, dy, dz = self.body["payload_envelope_mm"]
        payload = box(
            dx,
            dy,
            dz,
            -dx / 2,
            -dy / 2,
            self.body["electronics_deck_z_mm"] + self.body["electronics_deck_mm"][2],
        )
        self.component(
            "base_link",
            "payload_reference",
            payload,
            (0.28, 0.57, 0.32),
            "payload_reference",
            self.body["payload_mass_kg"],
        )
        ax, ay = self.body["hip_axis_xy_mm"]
        body_collisions = [self.prints["chassis"]["shape"], payload]
        self.frames = {}
        for prefix, sx, sy in [
            ("fl", 1, 1),
            ("fr", 1, -1),
            ("rl", -1, 1),
            ("rr", -1, -1),
        ]:
            phi = math.degrees(math.atan2(sy * ay, sx * ax))
            yaw_frame = place(
                sx * ax, sy * ay, self.body["hip_axis_z_mm"], self.rz(phi)
            )
            stator_frame = place(
                sx * ax, sy * ay, self.body["hip_axis_z_mm"], self.rz(phi + 180)
            )
            self.add_motor("base_link", prefix + "_yaw", stator_frame)
            body_collisions.append(
                transform(
                    box(
                        *self.s["case_mm"],
                        -self.s["shaft_from_front_mm"],
                        -self.s["case_mm"][1] / 2,
                        -self.s["case_mm"][2],
                    ),
                    stator_frame,
                )
            )
            coxa, femur, tibia, foot = [
                prefix + "_" + n for n in ("coxa", "femur", "tibia", "foot")
            ]
            for name in (coxa, femur, tibia, foot):
                self.link(name)
            self.joint(
                prefix + "_hip_yaw",
                "base_link",
                coxa,
                yaw_frame,
                limits=self.leg["yaw_limits_deg"],
            )
            self.component(
                coxa,
                prefix + "_coxa_print",
                self.prints["coxa_carrier"]["shape"],
                orange,
            )
            self.component(
                coxa,
                prefix + "_yaw_horn",
                self.metal_horn(),
                gray,
                "purchased_horn",
                self.s["horn_mass_kg"],
            )
            pitch_stator = place(
                self.leg["coxa_mm"], 0, self.leg["pitch_axis_rise_mm"], self.rx(-90)
            )
            self.add_motor(coxa, prefix + "_hip", pitch_stator)
            hip_frame = place(
                self.leg["coxa_mm"],
                0,
                self.leg["pitch_axis_rise_mm"],
                self.rx(-90).multiply(self.rz(self.leg["hip_neutral_deg"])),
            )
            self.joint(
                prefix + "_hip_pitch",
                coxa,
                femur,
                hip_frame,
                limits=self.leg["hip_limits_deg"],
            )
            self.component(
                femur,
                prefix + "_femur_print",
                self.prints["femur_carrier"]["shape"],
                orange,
            )
            self.component(
                femur,
                prefix + "_pitch_horn",
                self.metal_horn(),
                gray,
                "purchased_horn",
                self.s["horn_mass_kg"],
            )
            knee_stator = place(self.leg["femur_mm"], rotation=self.rz(180))
            self.add_motor(femur, prefix + "_knee", knee_stator)
            self.joint(
                prefix + "_knee_pitch",
                femur,
                tibia,
                place(
                    self.leg["femur_mm"], rotation=self.rz(self.leg["knee_neutral_deg"])
                ),
                limits=self.leg["knee_limits_deg"],
            )
            self.component(
                tibia, prefix + "_tibia_print", self.prints["tibia"]["shape"], orange
            )
            self.component(
                tibia,
                prefix + "_knee_horn",
                self.metal_horn(),
                gray,
                "purchased_horn",
                self.s["horn_mass_kg"],
            )
            self.joint(
                prefix + "_foot_fixed",
                tibia,
                foot,
                place(self.leg["tibia_mm"], 0, 8),
                kind="fixed",
            )
            self.component(
                foot,
                prefix + "_foot_cap",
                self.prints["foot_cap"]["shape"],
                (0.13, 0.16, 0.18),
                "TPU",
            )
            servo_box = box(
                *self.s["case_mm"],
                -self.s["shaft_from_front_mm"],
                -self.s["case_mm"][1] / 2,
                -self.s["case_mm"][2],
            )
            self.complete_link(coxa, [transform(servo_box, pitch_stator)])
            self.complete_link(femur, [transform(servo_box, knee_stator)])
            self.complete_link(tibia, [box(self.leg["tibia_mm"], 14, 4, 0, -7, 6)])
            self.complete_link(foot, [Part.makeSphere(self.leg["foot_radius_mm"])])
        self.complete_link("base_link", body_collisions)
        self.doc.recompute()
        self.robot.Proxy.set_joint_values(
            {j: 0.0 for j in self.joints if j.Type != "fixed"}
        )
        self.doc.recompute()

    def export_prints(self):
        folder = self.project / "print"
        folder.mkdir(exist_ok=True)
        for name, p in self.prints.items():
            shape = transform(p["shape"], place(rotation=p["rotation"]))
            bb = shape.BoundBox
            shape = transform(shape, place(-bb.XMin, -bb.YMin, -bb.ZMin))
            mesh = MeshPart.meshFromShape(
                Shape=shape,
                LinearDeflection=0.08,
                AngularDeflection=0.18,
                Relative=False,
            )
            bb = mesh.BoundBox
            dims = [bb.XLength, bb.YLength, bb.ZLength]
            if (
                max(dims[:2]) > min(self.cfg["printer_bed_mm"]) + 0.01
                or not mesh.isSolid()
            ):
                raise ValueError(
                    f"Print-bed fit or watertightness failed: {name}, {dims}"
                )
            mesh.write(str(folder / (name + ".stl")))
            self.print_reports[name] = {
                "quantity": p["quantity"],
                "material": p["material"],
                "dimensions_mm": dims,
                "volume_mm3": p["shape"].Volume,
                "facets": mesh.CountFacets,
                "closed": True,
                "notes": p["notes"],
            }
        with (self.project / "print_manifest.csv").open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(
                [
                    "part",
                    "quantity",
                    "material",
                    "bed_x_mm",
                    "bed_y_mm",
                    "height_mm",
                    "notes",
                ]
            )
            for name, p in self.print_reports.items():
                writer.writerow(
                    [
                        name,
                        p["quantity"],
                        p["material"],
                        *p["dimensions_mm"],
                        p["notes"],
                    ]
                )

    def export_robot(self):
        package_name = self.cfg["name"] + "_description"
        package = self.project / package_name
        (package / "urdf").mkdir(parents=True, exist_ok=True)
        xml = ET.Element("robot", {"name": self.cfg["name"]})
        xml.append(
            ET.Comment(
                (
                    "Prototype geometry and estimated composite inertia. See "
                    "design.json and manifest.json."
                )
            )
        )
        for name, link in self.links.items():
            element = link.Proxy.export_urdf(self.project, package_name)
            visuals = element.findall("visual")
            if len(visuals) != len(self.components[name]):
                raise ValueError(f"Unexpected visual count for {name}")
            for visual, c in zip(visuals, self.components[name]):
                visual.set("name", c["object"].Name)
                material = ET.SubElement(
                    visual, "material", {"name": c["material"] + "_" + c["object"].Name}
                )
                ET.SubElement(
                    material,
                    "color",
                    {"rgba": " ".join(str(v) for v in (*c["color"], 1))},
                )
            for i, collision in enumerate(element.findall("collision")):
                collision.set("name", name + f"_contact_{i}")
            xml.append(element)
        for joint in self.joints:
            element = joint.Proxy.export_urdf()
            if joint.Type != "fixed":
                ET.SubElement(
                    element,
                    "dynamics",
                    {
                        "damping": str(self.cfg["simulation"]["joint_damping"]),
                        "friction": "0",
                    },
                )
            xml.append(element)
        ET.indent(xml, space="  ")
        ET.ElementTree(xml).write(
            package / "urdf" / (self.cfg["name"] + ".urdf"),
            encoding="utf-8",
            xml_declaration=True,
        )
        (package / "package.xml").write_text(f"""<?xml version="1.0"?>
<package format="3"><name>{package_name}</name><version>0.1.0</version>
<description>Four-leg printable STS3215 prototype</description>
<maintainer email="local@example.invalid">Local operator</maintainer>
<license>Apache-2.0</license><buildtool_depend>ament_cmake</buildtool_depend>
<export><build_type>ament_cmake</build_type></export></package>
""")
        (package / "CMakeLists.txt").write_text(f"""cmake_minimum_required(VERSION 3.8)
project({package_name})
find_package(ament_cmake REQUIRED)
install(DIRECTORY urdf meshes DESTINATION share/${{PROJECT_NAME}})
ament_package()
""")

    def measure_and_check(self):
        all_bounds, foot_positions = [], {}
        for name, link in self.links.items():
            for c in self.components[name]:
                world = transform(c["shape"], link.Placement)
                bb = world.BoundBox
                all_bounds.append(
                    [bb.XMin, bb.YMin, bb.ZMin, bb.XMax, bb.YMax, bb.ZMax]
                )
            if name.endswith("_foot"):
                foot_positions[name] = list(link.Placement.Base)
        bounds = np.array(all_bounds)
        bbox = [bounds[:, :3].min(axis=0).tolist(), bounds[:, 3:].max(axis=0).tolist()]
        # Test unintended case/printed-part intersections at the neutral stance.
        cases, prints = [], []
        for name, components in self.components.items():
            for c in components:
                record = (
                    c["object"].Name,
                    transform(c["shape"], self.links[name].Placement),
                )
                if c["material"] == "purchased_servo":
                    cases.append(record)
                elif c["material"] == "PETG":
                    prints.append(record)
        interferences = []
        for case_name, case_shape in cases:
            for part_name, part_shape in prints:
                if not case_shape.BoundBox.intersect(part_shape.BoundBox):
                    continue
                volume = case_shape.common(part_shape).Volume
                if volume > 0.1:
                    interferences.append(
                        {"motor": case_name, "part": part_name, "overlap_mm3": volume}
                    )
        return bbox, foot_positions, interferences

    def finish(self):
        self.export_prints()
        self.export_robot()
        bbox, feet, interferences = self.measure_and_check()
        for obj in self.sources.Group:
            obj.Visibility = False
        self.robot.ViewObject.ShowReal = False
        self.robot.ViewObject.ShowVisual = True
        self.robot.ViewObject.ShowCollision = False
        self.robot.ViewObject.ShowJointAxes = False
        self.doc.recompute()
        Gui.Selection.clearSelection()
        Gui.activateWorkbench("CrossWorkbench")
        view = Gui.activeDocument().activeView()
        view.setCameraType("Orthographic")
        view.setCameraOrientation(
            (0.4247082003, 0.1759198966, 0.3398511430, 0.8204732386)
        )
        view.fitAll()
        Gui.updateGui()
        self.doc.saveAs(str(self.project / (self.cfg["name"] + ".FCStd")))
        view.saveImage(str(self.project / "preview.png"), 1400, 1000, "White")
        manifest = {
            "name": self.cfg["name"],
            "revision": self.cfg["revision"],
            "motors": 12,
            "actuated_joints": 12,
            "links": len(self.links),
            "body_frame_bounds_mm": bbox,
            "feet_in_body_frame_mm": feet,
            "initial_base_z_m": -bbox[0][2] / 1000
            + self.cfg["simulation"]["initial_floor_gap_m"],
            "estimated_mass_kg": sum(
                c["mass_kg"] for v in self.components.values() for c in v
            ),
            "print_parts": self.print_reports,
            "neutral_case_interferences": interferences,
            "links_metadata": {
                n: {
                    "mass_kg": l.Mass.Value,
                    "com_mm": list(l.CenterOfMass.Base),
                    "inertia_kg_m2": [l.Ixx, l.Ixy, l.Ixz, l.Iyy, l.Iyz, l.Izz],
                    "placement_position_mm": list(l.Placement.Base),
                    "placement_quaternion_xyzw": list(l.Placement.Rotation.Q),
                }
                for n, l in self.links.items()
            },
            "freecad_version": App.Version()[:3],
            "robotcad_commit": subprocess.check_output(
                ["git", "-C", "/workspace/output/robotcad/addon", "rev-parse", "HEAD"],
                text=True,
            ).strip(),
            "mass_qualification": self.cfg["print"]["mass_note"],
            "mechanical_status": (
                "Prototype; physical motor/horn/clamp and sweep fit tests "
                "required; not load-qualified"
            ),
        }
        (self.project / "manifest.json").write_text(json.dumps(manifest, indent=2))
        shutil.copyfile(TOOLS / "spider_sts3215.json", self.project / "design.json")
        if interferences:
            raise ValueError(f"Motor-case interference detected: {interferences}")
        (self.project / "build-result.json").write_text(
            json.dumps({"status": "ok", "project": str(self.project)}, indent=2)
        )
        App.closeDocument(self.doc.Name)


def build(project):
    """Generate the prototype in a new output directory."""
    if (project / "spider_sts3215.FCStd").exists():
        raise FileExistsError("Preserve the existing spider CAD file before rebuilding")
    builder = SpiderBuilder(project)
    builder.make_prints()
    builder.make_assembly()
    builder.finish()
