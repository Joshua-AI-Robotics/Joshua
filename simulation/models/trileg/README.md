Trileg Robot
============

A three-legged locomotion robot. The goal is to learn a walking gait
that moves the torso toward a target position (+X direction) using
only three legs arranged at 120-degree intervals.

Physical Structure
------------------

```
              thigh_a ── shin_a    (0°, along +X)
             /
  torso (sphere r=0.15)
       \
        thigh_b ── shin_b          (120°)
         \
          thigh_c ── shin_c        (240°)
```

| Body              | Shape   | Size                |
|-------------------|---------|---------------------|
| torso             | Sphere  | radius 0.15         |
| thigh_a/b/c (x3)  | Capsule | length 0.12, r 0.04 |
| shin_a/b/c (x3)   | Capsule | length 0.38, r 0.05 |

The torso carries the `PhysicsArticulationRootAPI` and all rigid bodies
have density 5. Thighs are colored slightly darker than their
corresponding shins for visual distinction.

Joints (6 DOF)
--------------

Each leg has two revolute joints:

| Joint    | Connects          | Axis       | Limits (deg) |
|----------|--------------------|-----------|--------------|
| yaw_a    | torso → thigh_a   | Z (vertical) | -45 to 45 |
| pitch_a  | thigh_a → shin_a  | Y (horizontal) | -20 to 90 |
| yaw_b    | torso → thigh_b   | Z (vertical) | -45 to 45 |
| pitch_b  | thigh_b → shin_b  | perp. to leg B | -20 to 90 |
| yaw_c    | torso → thigh_c   | Z (vertical) | -45 to 45 |
| pitch_c  | thigh_c → shin_c  | perp. to leg C | -20 to 90 |

- **Yaw joints** sweep the leg forward/backward in the horizontal plane.
- **Pitch joints** lift/lower the shin up/down relative to the thigh.

This 2-DOF-per-leg design gives the robot enough freedom to develop
proper walking gaits, unlike a single-DOF design where legs can only
pump up and down.

Initial Pose
------------

| Joint pattern | Angle (rad) |
|---------------|-------------|
| `yaw_.*`      | 0.0         |
| `pitch_.*`    | 0.9         |

Torso spawns at z = 0.45 m. With pitch at 0.9 rad (~52 degrees), the
shins angle downward to contact the ground, supporting the torso.

USD Asset
---------

`simulation/models/trileg/trileg_isaac.usda`

Config Presets
--------------

**None.** The `trileg_sim_isaac.pbtxt` preset was removed; the model assets here
are kept, but nothing ships that runs them.

Quick Start
-----------

Write a preset with `sim_backend: SIM_BACKEND_ISAAC_SIM` and `usd_filename`
pointing at `simulation/models/trileg/trileg_isaac.usda`, then:

```bash
bazel run //launcher:joshua_main -- --config <your-preset>.pbtxt
```

See [simulation/README.md](../../README.md) for the backend fields.

Physics Tuning
--------------

| Parameter          | Value | Purpose |
|--------------------|-------|---------|
| actuator_damping   | 15.0  | Physical resistance to fast joint movement |
| actuator_stiffness | 0.0   | Pure torque control (no position PD) |
