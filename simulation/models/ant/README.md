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

`simulation/models/ant_isaac.usda`

Config Presets
--------------

| Preset | Algorithm | Purpose |
|--------|-----------|---------|
| `ant_train_isaac_full_rsl_rl.pbtxt` | RSL-RL | Training (1000 iter) |
| `ant_train_isaac_full_skrl.pbtxt`   | skrl   | Training (1000 iter) |
| `ant_eval_isaac_full_rsl_rl.pbtxt`  | RSL-RL | Evaluation |
| `ant_eval_isaac_full_skrl.pbtxt`    | skrl   | Evaluation |

Quick Start
-----------

```bash
# Train with RSL-RL
bazel run //ai/train:trainer -- \
    --config config/config_preset/ant/ant_train_isaac_full_rsl_rl.pbtxt

# Train with skrl
bazel run //ai/train:trainer -- \
    --config config/config_preset/ant/ant_train_isaac_full_skrl.pbtxt

# Evaluate (update checkpoint_path in the pbtxt first)
bazel run //ai/train:trainer -- \
    --config config/config_preset/ant/ant_eval_isaac_full_rsl_rl.pbtxt
```

Reward Structure
----------------

| Term              | Weight  | Notes |
|-------------------|---------|-------|
| progress          | +1.0    | Forward distance toward target |
| alive             | +0.5    | Constant while not terminated |
| upright           | +0.1    | Bonus when torso Z-up > 0.93 |
| move_to_target    | +0.5    | Bonus when heading aligns > 0.8 |
| action_l2         | -0.005  | Penalizes large actions |
| energy            | -0.05   | Power consumption penalty |
| joint_pos_limits  | -0.1    | Penalty near joint limits |
