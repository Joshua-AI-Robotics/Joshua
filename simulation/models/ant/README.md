Ant Robot
=========

A four-legged locomotion robot modelled after the classic MuJoCo Ant.
The goal is to learn a walking gait that moves the torso toward a
target position (+X direction).

Physical Structure
------------------

```
                front_left_leg ── front_left_foot
               /
    torso (sphere r=0.25)
       |  \
       |   front_right_leg ── front_right_foot
       |
       |   left_back_leg  ── left_back_foot
        \
         right_back_leg ── right_back_foot
```

| Body              | Shape   | Size               |
|-------------------|---------|--------------------|
| torso             | Sphere  | radius 0.25        |
| *_leg (x4)        | Capsule | length 0.28, r 0.08 |
| *_foot (x4)       | Capsule | length 0.57, r 0.08 |

The torso carries the `PhysicsArticulationRootAPI` and all rigid bodies
have density 5.

Joints (8 DOF)
--------------

Each leg has two revolute joints:

| Joint              | Connects           | Axis | Limits (deg) |
|--------------------|--------------------|------|--------------|
| front_left_leg     | torso → FL leg     | Z    | -40 to 40    |
| front_left_foot    | FL leg → FL foot   | horiz | 30 to 100   |
| front_right_leg    | torso → FR leg     | Z    | -40 to 40    |
| front_right_foot   | FR leg → FR foot   | horiz | -100 to -30 |
| left_back_leg      | torso → LB leg     | Z    | -40 to 40    |
| left_back_foot     | LB leg → LB foot   | horiz | -100 to -30 |
| right_back_leg     | torso → RB leg     | Z    | -40 to 40    |
| right_back_foot    | RB leg → RB foot   | horiz | 30 to 100   |

The first joint in each leg (the `*_leg` joint) sweeps the leg
horizontally. The second joint (`*_foot` joint) lifts/lowers the foot.

Initial Pose
------------

| Joint pattern         | Angle (rad) |
|-----------------------|-------------|
| `.*_leg`              | 0.0         |
| `front_left_foot`     | 0.785       |
| `front_right_foot`    | -0.785      |
| `left_back_foot`      | -0.785      |
| `right_back_foot`     | 0.785       |

Torso spawns at z = 0.50 m.

USD Asset
---------

`simulation/models/ant/ant_isaac.usda` (the MuJoCo model is
`simulation/models/ant/ant.xml`)

Config Presets
--------------

| Preset | Backend | Purpose |
|--------|---------|---------|
| `ant_sim_interactive.pbtxt` | MuJoCo | Interactive 3D viewer |
| `ant_sim_isaac.pbtxt` | Isaac Sim | Isaac Sim viewer (requires Isaac Lab) |

Quick Start
-----------

```bash
# MuJoCo interactive viewer
bazel run //launcher:joshua_main -- \
    --config config/config_preset/ant/ant_sim_interactive.pbtxt

# Isaac Sim viewer (see simulation/README.md for prerequisites)
bazel run //launcher:joshua_main -- \
    --config config/config_preset/ant/ant_sim_isaac.pbtxt
```
