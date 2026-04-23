# Trajectory Export

Converts a trained Isaac Lab RL policy into a constant, open-loop trajectory
that can be replayed on a real robot via `trajectory_publisher.py`.

This is useful for robots without perception running simple periodic tasks
(e.g. walking). A trained policy is run closed-loop in simulation, joint
positions and actions are recorded, and the result is written as `.pbtxt`
waypoint files compatible with the existing ROS2 trajectory publisher.

## Quick Start

```bash
# Ant -- RSL-RL
bazel run //ai/train:trainer -- \
    --config config/config_preset/ant/ant_trajectory_export_rsl_rl.pbtxt

# Ant -- skrl
bazel run //ai/train:trainer -- \
    --config config/config_preset/ant/ant_trajectory_export_skrl.pbtxt

# Trileg -- RSL-RL
bazel run //ai/train:trainer -- \
    --config config/config_preset/trileg/trileg_trajectory_export_rsl_rl.pbtxt

# Trileg -- skrl
bazel run //ai/train:trainer -- \
    --config config/config_preset/trileg/trileg_trajectory_export_skrl.pbtxt
```

## Output Files

Output lands in the configured `output_dir` (default
`/tmp/joshua_checkpoints/trajectory_export/`):

| File | Format | Purpose |
|------|--------|---------|
| `trajectory_position.pbtxt` | Text proto | Waypoints using observed joint positions -- paste into robot config |
| `trajectory_torque.pbtxt` | Text proto | Waypoints using raw policy actions (torques) -- paste into robot config |
| `trajectory_position.npy` | NumPy | MuJoCo passive playback verification |
| `trajectory_torque.npy` | NumPy | MuJoCo passive playback verification |
| `trajectory_meta.json` | JSON | Metadata (joint names, topics, dt, duration) |

The `.pbtxt` files contain a complete `trajectories { ... }` block in the
same format as `python_spike_trajectory_example.pbtxt`, ready to paste into
any robot config that uses `trajectory_publisher.py`.

## Config Fields (`TrajectoryExportConfig`)

| Field | Default | Description |
|-------|---------|-------------|
| `checkpoint_path` | (required) | Path to trained `.pt` checkpoint |
| `algorithm` | `"rsl_rl"` | RL library (`"rsl_rl"` or `"skrl"`) |
| `warmup_steps` | 200 | Steps to skip before recording (lets gait stabilize) |
| `num_record_steps` | 1000 | Steps to record |
| `detect_cycle` | false | Auto-detect gait period via autocorrelation |
| `joint_topic_mappings` | (required) | Maps Isaac Lab joint names to ROS2 topics |
| `trajectory_node_id` | 1 | Node ID for `TRAJECTORY_PUBLISHER` in output |
| `output_dir` | `""` | Output directory (empty = default under checkpoint dir) |

### Joint-to-Topic Mapping

Each joint in the Isaac Lab environment must be mapped to a ROS2 topic so
the trajectory publisher knows where to send each command:

```protobuf
trajectory_export {
  joint_topic_mappings { joint_name: "hip_0"   topic: "/ant/joint_0/cmd" }
  joint_topic_mappings { joint_name: "ankle_0" topic: "/ant/joint_1/cmd" }
  # ... one entry per actuated joint
}
```

Joints without a mapping are skipped with a warning.

## End-to-End Workflow

1. **Train** -- run a training preset to produce a checkpoint.
2. **Export** -- run a trajectory export preset (must use the same
   `task_config`, `network`, and `sim_physics` as training).
3. **Verify** (optional) -- load `trajectory_position.npy` in MuJoCo
   passive mode to visually check the gait.
4. **Deploy** -- paste the `.pbtxt` trajectories block into your robot config.

## Gait Cycle Detection

When `detect_cycle: true`, the exporter uses autocorrelation on the
first joint's position signal to find the gait period. If a strong
periodic pattern is detected, only one full cycle is kept. This
produces a minimal, loopable trajectory.

If no clear cycle is found, the full recording is kept.

## Package Structure

```
trajectory_export/
  __init__.py            # Public API: run_trajectory_export()
  cycle_detection.py     # detect_gait_cycle() via autocorrelation
  pbtxt_writer.py        # write_trajectory_pbtxt() -- format .pbtxt output
  exporter.py            # export_trajectory_data() + run_trajectory_export()
  rsl_rl_backend.py      # RSL-RL checkpoint loading and simulation
  skrl_backend.py        # skrl checkpoint loading and simulation
```

This package runs inside Isaac Lab's Python environment (not under Bazel
directly). It is shipped as a `data` dependency of `isaac_launcher` and
imported by `isaac_runner.py` at runtime.

## Adding a New Robot

1. Train the robot and save a checkpoint.
2. Create a `<robot>_trajectory_export_<algo>.pbtxt` preset, copying an
   existing one (e.g. `ant_trajectory_export_rsl_rl.pbtxt`) and updating:
   - `checkpoint_path` to point to your checkpoint
   - `task_config` / `network` / `sim_physics` to match training
   - `joint_topic_mappings` for your robot's joints and topics
3. Run the export command.

## Adding a New RL Backend

1. Create `<backend>_backend.py` in this package, implementing a function
   with signature `trajectory_export_<backend>(cfg: dict) -> None` that
   records `all_positions` and `all_actions` arrays and calls
   `export_trajectory_data()`.
2. Add a dispatch branch in `exporter.py:run_trajectory_export()`.
