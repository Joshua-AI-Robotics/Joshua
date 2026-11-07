# FreeCAD and RobotCAD

Local CAD authoring and a reproducible two-link export example. FreeCAD runs
inside the existing Joshua Ubuntu 24 image with an isolated profile. The macros
use RobotCAD's link/joint exporters to produce printable STL parts and a ROS 2
description package containing URDF and Collada meshes. Nothing here is on the
Joshua runtime path; simulator assets are consumed by [simulation/](../../simulation/README.md).

For a four-leg, twelve-motor printable robot, see the
[STS3215 spider prototype guide](SPIDER_STS3215.md). Its source recipe targets
12 V STS3215-family motors and a 180 × 180 mm print bed, with CAD, STL,
URDF/meshes, and floating-base MuJoCo outputs under `output/robotcad/spider_sts3215/`.

## Quick start

Use an x86_64 Ubuntu desktop with Docker, `curl`, `git`, `sha256sum`, and `xauth`.
The GUI uses X11 or XWayland through `DISPLAY` and `XAUTHORITY`. Build Joshua's
image as described in [Getting Started](../../docs/GETTING_STARTED.md), then
run these commands from the repository root:

```bash
docker compose build joshua-u24
./scripts/setup_robotcad.sh
./scripts/run_robotcad.sh --create-demo
./scripts/run_robotcad.sh
```

Setup downloads the pinned official FreeCAD AppImage, checks its SHA-256,
checks out the pinned RobotCAD revision, and installs the locked Python
dependencies into the isolated profile. Re-running setup reuses the installation.
An existing addon checkout with local changes or a different revision is rejected
rather than overwritten. `JOSHUA_CAD_IMAGE` can select an existing compatible
image instead of `joshua:u24-jazzy`.

The create command closes FreeCAD when exports finish; the default command opens
the saved document for editing. Use `--detach` to leave the GUI running without
holding the terminal. Close the existing window before starting another session.
Creating the demo again refuses to overwrite an existing CAD file.

The GUI container has no network or robot-device mounts. Setup needs network
access to download software. CAD dependencies stay out of the host and Joshua's
runtime lockfiles. The optional RobotCAD MCP dependency is not installed.

## Outputs and editing

All downloaded software, profiles, X11 credentials, CAD documents, meshes,
reports, and previews stay under the Git-ignored `output/robotcad/` directory.
Only scripts, macros, version metadata, dependency pins, and documentation are
tracked.

```text
output/robotcad/
├── addon/                         pinned RobotCAD checkout
├── squashfs-root/                 extracted FreeCAD AppImage
├── home/                          isolated profile and dependencies
└── project/
    ├── robotcad_demo.FCStd
    ├── print/{base,arm}.stl
    ├── robotcad_demo_description/
    │   ├── urdf/robotcad_demo.urdf
    │   ├── meshes/*.dae
    │   ├── package.xml
    │   └── CMakeLists.txt
    ├── manifest.json
    └── export-validation.json
```

In FreeCAD select `robotcad_demo`, then change
**Data → JointValues → shoulder_joint_deg** between −90 and +90 degrees.
Expand `CAD_Sources` to inspect the primitive and Boolean features. To export a
printable body after editing, select `Base_Solid` or `Arm_Solid`, then choose
**File → Export → STL mesh**. Select the robot and use RobotCAD's **Export URDF**
for a fresh description package; `OutputPath` selects the export directory.
The native exporter also offers Gazebo/controller scaffolding that Joshua does
not require. Keep the meshes with their URDF.

The example uses a vertical hinge and 4.4 mm pivot holes. Base dimensions are
60 × 50 × 22 mm; the arm is approximately 118 × 20 × 6 mm. Example masses are
0.080 kg and 0.020 kg, with uniform-solid inertia calculated by RobotCAD.
Replace them with measured assembled-link properties for a real robot, accounting
for infill, motors, bearings, and fasteners. Joint effort and velocity limits are
illustrative. This example demonstrates authoring and export, not a qualified
mechanical design.

STL coordinates are millimeters. URDF and Collada use meters; angles are radians
and inertia is kg·m².

## Load the demo in MuJoCo

```bash
./scripts/run_robotcad.sh --convert-mujoco
./scripts/run_robotcad.sh --mujoco
```

Conversion reads the exported URDF, transforms the Collada scene geometry into
metric OBJ meshes, and generates `project/mujoco/{robot.xml,scene.xml,meshes/}`.
The scene adds a floor, lighting, a camera, and a position actuator. Its position
and effort limits come from the URDF. The base is fixed; the example position
servo gains are simulation settings, not an identified motor model. Conversion
checks joint/frame preservation, finite physics state, position response, and
actuator effort, saving `project/mujoco/validation.json`.

In the MuJoCo viewer, use the right-hand **Control** panel to change
`shoulder_joint_position` in radians. If the simulation is paused, press Space
to run it. The example travel is approximately −1.5708 to +1.5708 rad.
Use `--mujoco --detach` to leave the viewer running without holding the terminal.
The viewer and FreeCAD can run together in separate containers.

This converter targets the two-link demo and its Z-up Collada exports. It does
not handle arbitrary robot packages, textures, or calibrated actuator dynamics.
The generated `scene.xml` can also be selected through Joshua's
`simulation.model_path`; see [simulation/](../../simulation/README.md).

## Contents

- `versions.env` — FreeCAD version/checksum and RobotCAD commit.
- `requirements.lock` — the isolated Python 3.11 dependency set.
- `create_demo.FCMacro` — native CAD features, RobotCAD links/joint, inertia,
  STL exports, and URDF/mesh exports. Collision offsets are baked into simplified
  mesh shapes to avoid the upstream linked-primitive placement issue.
- `open_demo.FCMacro` — open the saved document in the RobotCAD workbench.
- `verify_exports.py` — independent checks specific to this demo.
- `spider_sts3215.json`, `spider_cad.py`, and `create_spider.FCMacro` — the
  four-leg design parameters, manufacturing geometry, and RobotCAD assembly.
- `verify_spider.py`, `check_spider.FCMacro`, `convert_spider.py`, and
  `package_spider.py` — independent export checks, sampled solid clearances,
  floating-base/fixture MuJoCo scenes, and an output archive with BOM and guide.
- `convert_mujoco.py` and `mujoco_scene.xml` — demo mesh conversion, generated
  MJCF, and a scene template; generated assets remain under `output/`.
- [setup_robotcad.sh](../../scripts/setup_robotcad.sh) and
  [run_robotcad.sh](../../scripts/run_robotcad.sh) — host entrypoints for setup,
  GUI authoring, and container-based verification.

## Verification

```bash
./scripts/run_robotcad.sh --verify-exports
```

This headless check validates STL dimensions, manifold edges and positive volume;
URDF link/joint structure, axis and limits; resolved mesh paths, metric bounds,
and alignment; and positive, physically admissible inertia tensors. It writes
`export-validation.json` under the generated project directory. A macro failure
writes `build-error.txt` or `open-error.txt` under the ignored output directory.

For the spider, `--check-cad` can also sample a supplied simultaneous-joint motion:
set `JOSHUA_CAD_MOTION_FILE` to a JSON file under the mounted CAD output directory,
using its container path. The JSON contains `poses`, each with `name` and a
`joints` mapping from all twelve URDF joint names to radians. Values outside the
CAD joint limits are rejected. Results go to `motion-clearance-validation.json`;
the ordinary clearance report and assembly images are preserved. This checks
the supplied solid poses, not every intermediate configuration or real cables.

## Responsibilities

- Preserve the CAD/export recipe and its software versions in source control.
- Generate both manufacturing and robot-description assets from the example.
- Check exported geometry and physical metadata independently of FreeCAD.

## Non-Goals

- Printer operation, firmware flashing, and physical robot control.
- Isaac import or a command bridge; see [simulation/](../../simulation/README.md).
- Changing Joshua's runtime configuration or managing ROS controllers.
