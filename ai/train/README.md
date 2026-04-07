AI Training Pipeline
====================

This directory contains the tools and infrastructure for collecting data, managing datasets, and training AI models for the Joshua platform.

DataStore (Data Collection)
---------------------------

### Overview
`ai/train/data_store.py` is the core data logging engine. It subscribes to ROS 2 topics in real-time, records them efficiently to `rosbag2` (SQLite3), and post-processes them into machine-learning-ready formats (Hugging Face Datasets, JSONL, CSV, Parquet).

### Key Features
-   **Robust Schema Handling**: Automatically discovers all possible fields across heterogeneous topics (e.g., Images + Encoder values) to ensure a consistent, crash-free dataset schema.
-   **Episode Indexing**: Maintains a persistent, auto-incrementing global episode counter across runs to prevent data overwrites and simplify merging.
-   **Real-time Control**: Supports dynamic Start/Stop recording via ROS topics or internal logic.
-   **Optimized Post-Processing**:
    -   Images are decoded to Numpy once and stored efficiently.
    -   Generic messages are converted to dictionaries.
    -   Sparse data handling: Missing fields are automatically filled with `None` to satisfy strict dataset schemas.

### Architecture
1.  **Recording (Online)**:
    -   Uses `rosbag2_py.SequentialWriter` for high-throughput, low-latency logging.
    -   Writes interleaved message streams (preserving exact timing).
2.  **Post-Processing (Offline/Shutdown)**:
    -   **Pass 1 (Discovery)**: Scans the bag to identify all unique topics and their field structures.
    -   **Pass 2 (Conversion)**: Streams the bag, enforces the unified schema, injects `episode_index`, and writes to the target format using `datasets.Dataset.from_generator`.

### Usage

**1. Configuration**
Define your data sources in a `.pbtxt` config file (e.g., `config/config_preset/example/sample_data_store.pbtxt`).

**2. Running the Data Subscriber**
The `ros2/data_subscriber.py` node wraps the DataStore.
```bash
# Launch the subscriber
bazel run launcher:joshua_main -- --config=config/config_preset/example/sample_data_store.pbtxt
```

**3. Controlling Recording**
Control the recording state via the `/recording_control` topic:
```bash
# Start Recording (Episode N)
ros2 topic pub --once /recording_control std_msgs/msg/Bool "{data: true}"

# Stop Recording
ros2 topic pub --once /recording_control std_msgs/msg/Bool "{data: false}"
```

**4. Data Inspection**
Use the provided utility to inspect generated datasets:
```bash
# View schema, metadata, and samples
bazel run ai/train:data_load -- --dataset_path=/tmp/Joshua/data/..._processed --num_samples=5
```

### Dataset Format
The output is an **Interleaved Message Stream**. Each row corresponds to a single ROS message event.
-   `topic`: The source topic name (e.g., `camera_1`, `encoder_joint_1`).
-   `timestamp`: Float (seconds).
-   `episode_index`: Integer ID for the recording session.
-   `image`: Numpy array (for camera topics, else None).
-   `data`: Scalar/Value (for standard messages, else None).
-   *(Other fields dynamically discovered from message types)*

*Note: For training (e.g., LeRobot, Octo), this interleaved data typically needs to be synchronized/resampled into state-action pairs.*

Extending Types (`ros2/ros2_type_resolver.py`)
------------------------------------------------
-   **New Message Types**: Add them to `ROS2_TYPE_MAPPING`.
-   **Special Handling**: Extend `build_entry_for_message` if you need custom decoding (like we do for Images) instead of generic dictionary conversion.

Training & Fine-Tuning
----------------------
*(Section to be expanded)*
-   **LeRobot Integration**: The dataset format is compatible with Hugging Face Datasets, making it a natural fit for the [LeRobot](https://github.com/huggingface/lerobot) framework.
-   **Preprocessing**: Use `LeRobotDataset` to synchronize the interleaved `DataStore` output into `(observation, action)` batches for training.


Isaac Sim / Isaac Lab
---------------------

### What Are They?

**Isaac Sim** is NVIDIA's GPU-accelerated physics simulator built on
Omniverse.  It uses PhysX for rigid/soft-body simulation and renders
via ray-tracing.  Assets are described in USD (Universal Scene
Description) format.

**Isaac Lab** is a robotics RL framework built on top of Isaac Sim.
It provides:

- `ManagerBasedRLEnvCfg` -- declarative environment definitions
  (scene, observations, rewards, terminations, actions, events)
- Gym-compatible vectorized environments (`gymnasium.make()`)
- Built-in RL library integrations (RSL-RL, skrl, rl_games, SB3)
- A task registry (`gym.register()`) for environment + agent configs

```
Isaac Sim  (simulator engine, PhysX, USD, rendering)
    └── Isaac Lab  (RL framework, env configs, task registry)
          └── Joshua  (config-driven launcher, term-based tasks)
```

### Why a Separate Process?

Isaac Sim bundles its own Python 3.11, numpy<2, and hundreds of
Omniverse packages.  Joshua uses Bazel-managed Python 3.10.  These
cannot coexist in a single process.

Joshua solves this by running Isaac Lab in a **subprocess** with its
own virtual environment, communicating via a JSON config file written
by `isaac_launcher.py`.

### Prerequisites

#### 1. Install Isaac Sim

Follow NVIDIA's official guide:
https://docs.isaacsim.omniverse.nvidia.com/latest/installation/index.html

Isaac Sim 4.5+ is recommended.  Requires an NVIDIA GPU with driver
version compatible with the chosen Isaac Sim release.

#### 2. Clone Isaac Lab

```bash
cd ~
git clone https://github.com/isaac-sim/IsaacLab.git
cd IsaacLab
```

#### 3. Create the Virtual Environment

```bash
cd ~/IsaacLab
./isaaclab.sh --install
```

Or manually:

```bash
python3.11 -m venv ~/env_isaaclab
source ~/env_isaaclab/bin/activate
pip install isaacsim-rl isaacsim-replicator isaacsim-extscache-physics
pip install -e ~/IsaacLab/source/isaaclab
pip install -e ~/IsaacLab/source/isaaclab_tasks
```

#### 4. Set Environment Variables

```bash
export ISAAC_LAB_PATH=~/IsaacLab
export ISAAC_LAB_PYTHON=~/env_isaaclab/bin/python
```

### Running Training

The recommended way is through the unified `joshua_main` launcher, which
dispatches to the trainer automatically when the config uses
`operation_mode: MODE_TRAINING`:

```bash
# Ant (RSL-RL) -- via unified launcher
bazel run //launcher:joshua_main -- \
    --config config/config_preset/ant/ant_train_isaac_full_rsl_rl.pbtxt

# Trileg (skrl) -- via unified launcher
bazel run //launcher:joshua_main -- \
    --config config/config_preset/trileg/trileg_train_isaac_full_skrl.pbtxt
```

Direct invocation also works for development:

```bash
# Ant (RSL-RL) -- direct
bazel run //ai/train:trainer -- \
    --config config/config_preset/ant/ant_train_isaac_full_rsl_rl.pbtxt
```

### Running Evaluation

```bash
# Ant (RSL-RL) -- update checkpoint_path in the pbtxt first
bazel run //launcher:joshua_main -- \
    --config config/config_preset/ant/ant_eval_isaac_full_rsl_rl.pbtxt

# Direct invocation also works:
bazel run //ai/train:trainer -- \
    --config config/config_preset/ant/ant_eval_isaac_full_rsl_rl.pbtxt
```

### Supported RL Libraries

| Algorithm | Library | Config value | Notes |
|-----------|---------|-------------|-------|
| PPO | RSL-RL | `algorithm: "rsl_rl"` | On-policy, higher sample efficiency |
| PPO | skrl | `algorithm: "skrl"` | On-policy, different default hyperparams |


How It Works
------------

All robot tasks are defined entirely in `.pbtxt` configuration files --
no Python task files are needed. The pipeline flows like this:

```
.pbtxt preset
    │
    ▼
joshua_main         (C++ launcher)
    │  Loads config, sees MODE_TRAINING
    │  Resolves and fork/execs trainer binary
    ▼
trainer.py          (Bazel / Python 3.10)
    │  Parses proto, dispatches to isaac_launcher.py
    ▼
isaac_launcher.py   (Bazel / Python 3.10)
    │  Serializes RLConfig → JSON, spawns subprocess
    ▼
isaac_runner.py     (Isaac Lab venv / Python 3.11)
    │  Reads JSON, calls task_builder.py
    ▼
task_builder.py     (Isaac Lab venv / Python 3.11)
    │  Builds robot, rewards, observations from config
    │  Registers gym environment dynamically
    ▼
Isaac Lab PPO training loop (RSL-RL or skrl)
    │
    ▼
Checkpoints saved to /tmp/joshua_checkpoints/
```

Ctrl+C (SIGINT) cleanly propagates through the entire process tree --
`joshua_main` forwards the signal to the trainer's process group, which
in turn terminates Isaac Sim and all child processes.


Configuration Reference
------------------------

All training parameters live in `ai/proto/training.proto` and are set
via `.pbtxt` presets in `config/config_preset/`, organized by robot.

### task_config (robot + environment definition)

```protobuf
task_config {
  task_name: "my_robot"
  robot {
    usd_filename: "my_robot_isaac.usda"
    init_pos_z: 0.5
    init_joint_pos { key: "hip_.*"  value: 0.0 }
    init_joint_pos { key: "knee_.*" value: -0.5 }
    actuator_stiffness: 0.0
    actuator_damping: 5.0
  }
  rewards {
    key: "progress"
    value { weight: 1.0 }
  }
  rewards {
    key: "alive"
    value { weight: 0.5 }
  }
  observations {
    key: "base_lin_vel"
    value {}
  }
  observations {
    key: "joint_pos_norm"
    value {}
  }
  observations {
    key: "actions"
    value {}
  }
}
```

### ppo (PPO hyperparameters)

```protobuf
ppo {
  learning_rate: 5e-4
  gamma: 0.99
  gae_lambda: 0.95
  clip_epsilon: 0.2
  entropy_coef: 0.0
  num_learning_epochs: 5
  num_minibatches: 4
  num_steps_per_env: 32
  desired_kl: 0.01
  max_grad_norm: 1.0
  schedule: "adaptive"
}
```

### network (actor-critic architecture)

```protobuf
network {
  actor_hidden_dims: 400
  actor_hidden_dims: 200
  actor_hidden_dims: 100
  critic_hidden_dims: 400
  critic_hidden_dims: 200
  critic_hidden_dims: 100
  activation: "elu"
  init_noise_std: 1.0
}
```

### sim_physics (simulation parameters)

```protobuf
sim_physics {
  decimation: 2
  episode_length_s: 16.0
  sim_dt: 0.00833
  action_scale: 7.5
  env_spacing: 5.0
  terrain_friction: 1.0
  bounce_threshold_velocity: 0.2
}
```

### termination / reset

```protobuf
termination {
  min_root_height: 0.31
}
reset {
  joint_pos_range_min: -0.2
  joint_pos_range_max: 0.2
  joint_vel_range_min: -0.1
  joint_vel_range_max: 0.1
}
```

### eval (evaluation config)

Evaluation presets mirror the training `task_config` and include all
the same blocks (task_config, network, sim_physics, termination, reset,
ppo) so the environment is reconstructed identically:

```protobuf
eval {
  algorithm: "rsl_rl"
  checkpoint_path: "/tmp/joshua_checkpoints/isaac_logs/my_model/model_999.pt"
  num_episodes: 10
  num_envs: 4
  render: true

  task_config { ... }
  network { ... }
  sim_physics { ... }
  termination { ... }
  reset { ... }
}
```


How to Add a New Robot
----------------------

1. **Create a USD asset** at `simulation/models/my_robot_isaac.usda`
   describing the robot's bodies, collision geometry, and joints.

2. **Create a `.pbtxt` preset** with `task_config` defining the full
   environment:

```bash
config/config_preset/my_robot/my_robot_train_isaac_full_rsl_rl.pbtxt
```

The preset must contain:
- `task_config.robot` -- USD path, initial pose, joint positions, actuator params
- `task_config.rewards` -- reward terms and weights
- `task_config.observations` -- observation terms
- `ppo` / `network` / `sim_physics` / `termination` / `reset` blocks

See the existing ant and trileg presets for complete examples.

3. **Run training**:

```bash
bazel run //launcher:joshua_main -- \
    --config config/config_preset/my_robot/my_robot_train_isaac_full_rsl_rl.pbtxt
```

4. **Create an eval preset** that mirrors the training config but uses
   `TRAINING_METHOD_EVAL` and adds `checkpoint_path`.

5. **(Optional)** Create a `README.md` in `simulation/models/my_robot/`
   documenting the robot anatomy and joint structure.


Available Reward Terms
----------------------

| Term name | Parameters | Description |
|-----------|-----------|-------------|
| `progress` | `target_pos` | Distance toward target |
| `alive` | -- | Constant bonus while alive |
| `upright` | `threshold` | Bonus when Z-up projection > threshold |
| `move_to_target` | `threshold`, `target_pos` | Bonus when heading aligns with target |
| `action_l2` | -- | Penalizes large actions |
| `energy` | `gear_ratios` | Power consumption penalty |
| `joint_pos_limits` | `threshold`, `gear_ratios` | Penalty near joint limits |
| `lin_vel_z` | -- | Penalizes vertical velocity |
| `ang_vel_xy` | -- | Penalizes roll/pitch angular velocity |
| `flat_orientation` | -- | Penalizes deviation from flat |
| `action_rate` | -- | Penalizes rapid action changes |
| `joint_vel` | -- | Penalizes high joint velocities |

Available Observation Terms
---------------------------

| Term name | Parameters | Description |
|-----------|-----------|-------------|
| `base_height` | -- | Root body Z position |
| `base_lin_vel` | -- | Root body linear velocity |
| `base_ang_vel` | -- | Root body angular velocity |
| `base_yaw_roll` | -- | Yaw and roll angles |
| `base_angle_to_target` | `target_pos` | Angle to target position |
| `base_up_proj` | -- | Projection of up vector onto Z |
| `base_heading_proj` | `target_pos` | Heading alignment with target |
| `joint_pos_norm` | -- | Normalized joint positions |
| `joint_vel_rel` | `scale` | Scaled joint velocities |
| `body_forces` | `body_names`, `scale` | Contact forces on named bodies |
| `actions` | -- | Previous action values |


Available Robots
----------------

| Robot | DOF | USD | Documentation |
|-------|-----|-----|---------------|
| Ant | 8 (4 legs x 2 joints) | `simulation/models/ant_isaac.usda` | `simulation/models/ant/README.md` |
| Trileg | 6 (3 legs x 2 joints) | `simulation/models/trileg_isaac.usda` | `simulation/models/trileg/README.md` |


Key Files
---------

| File | Runs in | Purpose |
|------|---------|---------|
| `launcher/joshua_main.cc` | Joshua (C++) | Unified entry point, dispatches by operation mode |
| `launcher/training_launcher.cc` | Joshua (C++) | Resolves and fork/execs the trainer binary |
| `ai/train/trainer.py` | Joshua (Bazel) | Training dispatcher (RL, imitation, eval) |
| `ai/train/isaac_launcher.py` | Joshua (Bazel) | Config serialization, subprocess launch |
| `ai/train/isaac_runner.py` | Isaac Lab (venv) | Training/eval bridge |
| `ai/train/isaac_lab/task_builder.py` | Isaac Lab (venv) | Generic proto-driven task builder |
| `ai/train/isaac_lab/env_builder.py` | Isaac Lab (venv) | ManagerBasedRLEnvCfg builder |
| `ai/train/isaac_lab/rsl_rl_config.py` | Isaac Lab (venv) | RSL-RL agent config builder |
| `ai/train/isaac_lab/terms/` | Isaac Lab (venv) | Reusable MDP term factories |
| `ai/proto/training.proto` | Both | Full config schema |
| `simulation/models/*.usda` | Isaac Lab (venv) | Local USD robot assets |
| `config/config_preset/**/*.pbtxt` | Joshua (Bazel) | Training/eval presets (organized by robot) |


Troubleshooting
---------------

| Problem | Fix |
|---------|-----|
| `OSError: Isaac Lab not found` | Set `ISAAC_LAB_PATH` or `ISAAC_LAB_PYTHON` env var |
| `Error: Command failed to spawn: Aborted` | Check `nvidia-smi`; verify GPU driver compatibility |
| Training much slower than native Isaac Lab | Check `max_iterations` -- Joshua converts it to `max_iterations * rollout_steps` timesteps |
| `ModuleNotFoundError: No module named 'pxr'` | Expected inside Bazel; USD tools only work in the Isaac Lab venv |
| `Unknown reward term 'foo'` | Check `task_builder.py` `REWARD_REGISTRY` for available term names |
| `AttributeError: 'tuple' object has no attribute 'get'` | Mismatch between reset event parameter types; check `_apply_env_overrides` in `isaac_runner.py` |


Future Work
-----------

### Additional RL library backends

Currently only **RSL-RL** and **skrl** are implemented. Isaac Lab also
supports rl_games and Stable Baselines 3. Adding them requires:

- [ ] **rl_games**: Add `rl_games_config.py` in `isaac_lab/` (similar
  to `rsl_rl_config.py` -- builds a configclass for the rl_games
  runner). Wire up `_train_rl_games()` and `_eval_rl_games()` in
  `isaac_runner.py`. Register the config via
  `rl_games_cfg_entry_point` in `task_builder.py`.

- [ ] **Stable Baselines 3 (SB3)**: Add `sb3_config.py` in
  `isaac_lab/`. Wire up `_train_sb3()` and `_eval_sb3()` in
  `isaac_runner.py`. Register via `sb3_cfg_entry_point`.

- [ ] **SAC / TD3 (off-policy)**: skrl and SB3 both support off-policy
  algorithms. Extend the proto `algorithm` field to accept `"sac"`,
  `"td3"`, etc. and add corresponding config messages.

### Environment and training enhancements

- [ ] **Domain randomization**: Wire `DomainRandomizationConfig` proto
  fields to randomize physics parameters (mass, friction, joint
  damping) during training for sim-to-real transfer.

- [ ] **Curriculum learning**: Wire `CurriculumConfig` proto fields to
  progressively increase task difficulty (e.g., terrain roughness,
  target distance, episode length).

- [ ] **Terrain generation**: Wire `TerrainConfig` proto fields to
  generate procedural terrains (stairs, slopes, rough ground) via
  Isaac Lab's terrain generator.

- [ ] **Observation noise**: Wire `ObservationNoiseConfig` proto fields
  to inject sensor noise into observations for robustness.

- [ ] **Multi-agent training**: Support multiple robots in the same
  environment for cooperative or competitive tasks.

- [ ] **Resume training**: Wire `ResumeConfig` proto fields to resume
  from a checkpoint with potentially modified hyperparameters.

### Robot assets

- [ ] **Quadruped**: 4-legged robot with 3-DOF legs (hip yaw, hip
  pitch, knee pitch) -- 12 DOF total.

- [ ] **Hexapod**: 6-legged robot with 2-DOF legs -- 12 DOF total.

- [ ] **Biped / Humanoid**: Custom humanoid with upper body for
  manipulation tasks.

- [ ] **Wheeled robots**: Differential drive or omnidirectional robots
  for navigation tasks.
