MDP Term Library
================

Reusable MDP (Markov Decision Process) term factories for composing
Isaac Lab environments. Each factory returns an Isaac Lab `TermCfg`
object ready to plug into `build_env_cfg()`.

All terms are **robot-agnostic** -- they operate on the generic
articulation root body and joint states that every USD robot provides.


How Terms Work
--------------

```
.pbtxt preset
  rewards { key: "progress" value { weight: 2.0 } }
       │
       ▼
task_builder.py
  REWARD_REGISTRY["progress"] → terms.progress_reward
       │
       ▼
terms/rewards.py
  progress_reward(weight=2.0)
       │
       ▼
Isaac Lab RewardTermCfg(func=_progress_reward, weight=2.0, ...)
```

The `task_builder.py` registries (`REWARD_REGISTRY`, `OBSERVATION_REGISTRY`)
map string names from `.pbtxt` configs to term factory functions in this
library. Adding a new term requires:

1. Implement the factory in the appropriate module
2. Export it from `__init__.py`
3. Register it in `task_builder.py`


Current Terms
-------------

### Rewards (`rewards.py`)

| Registry key       | Factory               | Parameters                | Source |
|--------------------|-----------------------|---------------------------|--------|
| `progress`         | `progress_reward`     | `weight`, `target_pos`    | humanoid mdp |
| `alive`            | `is_alive`            | `weight`                  | isaaclab mdp |
| `upright`          | `upright_posture`     | `weight`, `threshold`     | humanoid mdp |
| `move_to_target`   | `move_to_target`      | `weight`, `threshold`, `target_pos` | humanoid mdp |
| `action_l2`        | `action_l2`           | `weight`                  | isaaclab mdp |
| `energy`           | `power_consumption`   | `weight`, `gear_ratios`   | humanoid mdp |
| `joint_pos_limits` | `joint_pos_limits`    | `weight`, `threshold`, `gear_ratios` | humanoid mdp |
| `lin_vel_z`        | `lin_vel_z_l2`        | `weight`                  | isaaclab mdp |
| `ang_vel_xy`       | `ang_vel_xy_l2`       | `weight`                  | isaaclab mdp |
| `flat_orientation` | `flat_orientation_l2` | `weight`                  | isaaclab mdp |
| `action_rate`      | `action_rate_l2`      | `weight`                  | isaaclab mdp |
| `joint_vel`        | `joint_vel_l2`        | `weight`                  | isaaclab mdp |

### Observations (`observations.py`)

| Registry key           | Factory                | Parameters            | Source |
|------------------------|------------------------|-----------------------|--------|
| `base_height`          | `base_pos_z`           | --                    | isaaclab mdp |
| `base_lin_vel`         | `base_lin_vel`         | --                    | isaaclab mdp |
| `base_ang_vel`         | `base_ang_vel`         | --                    | isaaclab mdp |
| `base_yaw_roll`        | `base_yaw_roll`        | --                    | humanoid mdp |
| `base_angle_to_target` | `base_angle_to_target` | `target_pos`          | humanoid mdp |
| `base_up_proj`         | `base_up_proj`         | --                    | humanoid mdp |
| `base_heading_proj`    | `base_heading_proj`    | `target_pos`          | humanoid mdp |
| `joint_pos_norm`       | `joint_pos_normalized` | --                    | isaaclab mdp |
| `joint_vel_rel`        | `joint_vel_rel`        | `scale`               | isaaclab mdp |
| `body_forces`          | `body_forces`          | `body_names`, `scale` | isaaclab mdp |
| `actions`              | `last_action`          | --                    | isaaclab mdp |

### Terminations (`terminations.py`)

| Factory              | Parameters       | Source |
|----------------------|------------------|--------|
| `time_out`           | --               | isaaclab mdp |
| `root_height_below`  | `minimum_height` | isaaclab mdp |

### Events (`events.py`)

| Factory                | Parameters                    | Source |
|------------------------|-------------------------------|--------|
| `reset_root_state`     | `pose_range`, `velocity_range` | isaaclab mdp |
| `reset_joints_by_offset` | `pos_range`, `vel_range`    | isaaclab mdp |

### Actions (`actions.py`)

| Factory                | Parameters                      | Source |
|------------------------|---------------------------------|--------|
| `joint_effort_actions` | `scale`, `joint_names`, `asset_name` | isaaclab mdp |


Import Sources
--------------

Terms marked **"isaaclab mdp"** come from `isaaclab.envs.mdp` -- the
generic MDP module that works with any articulated robot.

Terms marked **"humanoid mdp"** come from
`isaaclab_tasks.manager_based.classic.humanoid.mdp`. Despite the path,
these functions are **not** humanoid-specific. They operate on the
generic articulation root body state (position, orientation, velocity)
and joint states that every USD robot provides. They live in the
humanoid package because that's where Isaac Lab first implemented
locomotion rewards.


Future Work
-----------

### Terminations

- [ ] **`bad_orientation`**: Terminate when the robot's up-vector Z
  projection drops below a threshold (robot has fallen over).
  Wraps `isaaclab.envs.mdp.bad_orientation` with configurable
  `limit` parameter.

- [ ] **`joint_pos_out_of_limit`**: Terminate when any joint exceeds
  its position limits. Wraps
  `isaaclab.envs.mdp.joint_pos_out_of_limit`.

- [ ] **`joint_pos_out_of_manual_limit`**: Terminate when joints
  exceed user-specified soft limits (tighter than USD limits).
  Wraps `isaaclab.envs.mdp.joint_pos_out_of_manual_limit`.

- [ ] **`joint_vel_out_of_limit`**: Terminate when joint velocities
  exceed their limits.

- [ ] **`illegal_contact`**: Terminate when forbidden bodies touch
  the ground (e.g. torso hitting the floor). Needs configurable
  `body_names` and `threshold`.

### Rewards

- [ ] **`base_lin_vel_xy_tracking`**: Reward for tracking a desired
  XY velocity command. Essential for velocity-conditioned policies.

- [ ] **`base_ang_vel_z_tracking`**: Reward for tracking a desired
  yaw rate. Pairs with velocity tracking for omnidirectional
  locomotion.

- [ ] **`feet_air_time`**: Reward for keeping feet in the air for a
  target duration -- encourages proper gait cycles.

- [ ] **`feet_contact_forces`**: Penalty when foot contact forces
  exceed a maximum threshold. Encourages gentle ground contact.

- [ ] **`joint_torques_l2`**: Direct penalty on joint torques
  (distinct from energy which is torque * velocity).

- [ ] **`joint_acceleration_l2`**: Penalty on joint accelerations
  for smoother motion.

- [ ] **`base_height_tracking`**: Reward for maintaining a target
  base height. Useful for crouching/standing tasks.

- [ ] **`stumble`**: Penalty when feet contact forces spike
  unexpectedly, indicating a stumble.

### Observations

- [ ] **`base_pos_xy`**: Root body XY position (for navigation
  tasks where absolute position matters).

- [ ] **`joint_pos_raw`**: Raw (un-normalized) joint positions.

- [ ] **`joint_torques`**: Current joint torques applied by
  actuators.

- [ ] **`foot_contact_binary`**: Binary contact state per foot
  (0 or 1). Needs configurable `body_names`.

- [ ] **`terrain_height`**: Height map samples around the robot for
  terrain-aware locomotion.

- [ ] **`velocity_commands`**: External velocity command input for
  velocity-conditioned policies (vx, vy, omega_z).

### Events

- [ ] **`push_robot`**: Apply random external forces to the robot
  during training for disturbance rejection.

- [ ] **`randomize_mass`**: Randomize body masses for domain
  randomization.

- [ ] **`randomize_friction`**: Randomize terrain friction for
  sim-to-real robustness.

- [ ] **`randomize_joint_properties`**: Randomize joint stiffness
  and damping during training.

### Actions

- [ ] **`joint_position_actions`**: Position-controlled joints
  (PD controller) instead of direct torque/effort.

- [ ] **`joint_velocity_actions`**: Velocity-controlled joints.

- [ ] **`binary_joint_actions`**: On/off joint actions for grippers
  or simple mechanisms.
