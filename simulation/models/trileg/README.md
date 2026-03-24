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

`simulation/models/trileg_isaac.usda`

Config Presets
--------------

| Preset | Algorithm | Purpose |
|--------|-----------|---------|
| `trileg_train_isaac_full_rsl_rl.pbtxt` | RSL-RL | Training (1500 iter) |
| `trileg_train_isaac_full_skrl.pbtxt`   | skrl   | Training (1500 iter) |
| `trileg_eval_isaac_full_rsl_rl.pbtxt`  | RSL-RL | Evaluation |
| `trileg_eval_isaac_full_skrl.pbtxt`    | skrl   | Evaluation |

Quick Start
-----------

```bash
# Train with RSL-RL
bazel run //ai/train:trainer -- \
    --config config/config_preset/trileg_train_isaac_full_rsl_rl.pbtxt

# Train with skrl
bazel run //ai/train:trainer -- \
    --config config/config_preset/trileg_train_isaac_full_skrl.pbtxt

# Evaluate (update checkpoint_path in the pbtxt first)
bazel run //ai/train:trainer -- \
    --config config/config_preset/trileg_eval_isaac_full_rsl_rl.pbtxt
```

Reward Structure
----------------

| Term              | Weight  | Notes |
|-------------------|---------|-------|
| progress          | +2.0    | Forward distance toward target |
| alive             | +0.2    | Constant while not terminated |
| upright           | +0.1    | Bonus when torso Z-up > 0.93 |
| move_to_target    | +0.5    | Bonus when heading aligns > 0.8 |
| lin_vel_z         | -0.05   | Penalizes vertical bouncing |
| ang_vel_xy        | -0.05   | Penalizes body roll/pitch rate |
| flat_orientation  | -0.05   | Penalizes tilting away from flat |
| action_rate       | -0.01   | Penalizes rapid action changes |
| joint_vel         | -0.0005 | Penalizes high joint velocities |
| action_l2         | -0.005  | Penalizes large actions |
| energy            | -0.05   | Power consumption (gear_ratio 1.0) |
| joint_pos_limits  | -0.1    | Penalty near joint limits (gear_ratio 1.0) |

Design Notes
------------

The trileg has lighter penalties on `lin_vel_z` and `flat_orientation`
compared to a humanoid because a tripod naturally bobs vertically and
tilts during locomotion. The `energy` and `joint_pos_limits` terms use
`gear_ratio: 1.0` instead of the humanoid default (15.0) to avoid
over-penalizing the smaller actuators.
