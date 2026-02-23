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
Define your data sources in a `.pbtxt` config file (e.g., `config/config_preset/sample_data_store.pbtxt`).

**2. Running the Data Subscriber**
The `ros2/data_subscriber.py` node wraps the DataStore.
```bash
# Launch the subscriber
bazel run launcher:joshua_main -- --config=config/config_preset/sample_data_store.pbtxt
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


Simulator Backends
------------------

Joshua supports two simulator backends for RL training.  The backend
is selected per-config via `simulator_backend`:

| Backend | Engine | Python | Best for |
|---------|--------|--------|----------|
| `SIM_BACKEND_MJX` | MuJoCo XLA (JAX) | Bazel-managed 3.10 | Fast iteration, JAX-native, massively parallel on GPU |
| `SIM_BACKEND_ISAAC_SIM` | Isaac Sim (PhysX) | Separate venv 3.11 | High-fidelity rendering, USD assets, domain randomization |

```
trainer.py (Bazel, Python 3.10)
  ├── SIM_BACKEND_MJX       → in-process   → mjx_rl.py  → MJXEnv
  └── SIM_BACKEND_ISAAC_SIM → subprocess   → isaac_runner.py (Isaac Lab venv)
```


Isaac Sim / Isaac Lab
---------------------

### What Are They?

**Isaac Sim** is NVIDIA's GPU-accelerated physics simulator built on
Omniverse.  It uses PhysX for rigid/soft-body simulation and renders
via ray-tracing.  Assets are described in USD (Universal Scene
Description) format.

**Isaac Lab** is a robotics RL framework built on top of Isaac Sim.
It provides:

- `ManagerBasedRLEnvCfg` — declarative environment definitions
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
Omniverse packages.  Joshua uses Bazel-managed Python 3.10 with
JAX / numpy>=2.  These cannot coexist in a single process.

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

Isaac Lab provides a setup script that creates a venv with all
required dependencies:

```bash
cd ~/IsaacLab

# Create venv and install Isaac Lab + dependencies
./isaaclab.sh --install
```

This creates a venv (typically at `~/IsaacLab/_isaac_sim/` for
binary installs, or you can create one manually):

```bash
# Manual venv creation (if using pip-based Isaac Sim)
python3.11 -m venv ~/env_isaaclab
source ~/env_isaaclab/bin/activate
pip install isaacsim-rl isaacsim-replicator isaacsim-extscache-physics
pip install -e ~/IsaacLab/source/isaaclab
pip install -e ~/IsaacLab/source/isaaclab_tasks
```

#### 4. Verify Isaac Lab Works

```bash
# Using isaaclab.sh
cd ~/IsaacLab
./isaaclab.sh -p -c "import isaaclab; print(isaaclab.__version__)"

# Or using the venv directly
~/env_isaaclab/bin/python -c "import isaaclab; print('OK')"

# Run a quick sanity check (headless Ant training, 10 iterations)
./isaaclab.sh -p scripts/reinforcement_learning/skrl/train.py \
    --task Isaac-Ant-v0 --headless --max_iterations 10
```

#### 5. Set Environment Variables for Joshua

Joshua needs to locate Isaac Lab's Python interpreter.
**Recommended:** set `ISAAC_LAB_PYTHON` to the venv Python binary.
This is more reliable than `ISAAC_LAB_PATH` because Bazel sanitizes
the subprocess environment, which can break `isaaclab.sh`.

```bash
export ISAAC_LAB_PATH=~/IsaacLab
export ISAAC_LAB_PYTHON=~/env_isaaclab/bin/python
```

For persistence, add to your shell profile:

```bash
echo 'export ISAAC_LAB_PATH=~/IsaacLab' >> ~/.bashrc
echo 'export ISAAC_LAB_PYTHON=~/env_isaaclab/bin/python' >> ~/.bashrc
source ~/.bashrc
```

### Running Training

```bash
bazel run //ai/train:trainer -- \
    --config config/config_preset/ant_train_isaac.pbtxt
```

Example config (`ant_train_isaac.pbtxt`):

```protobuf
general {
  operation_mode: MODE_TRAINING
  name: "ant"
}
ai {
  training {
    environment: TRAINING_ENV_SIMULATION
    method: TRAINING_METHOD_RL
    simulator_backend: SIM_BACKEND_ISAAC_SIM
    rl {
      task: "ant"
      algorithm: "skrl"          # or "rsl_rl"
      max_iterations: 500        # learning iterations (not timesteps)
      num_envs: 4096
      save_path: "ant_isaac_skrl_ppo"
      render: false
    }
  }
}
```

### Running Evaluation

```bash
bazel run //ai/train:trainer -- \
    --config config/config_preset/ant_eval_isaac.pbtxt
```

Example config (`ant_eval_isaac.pbtxt`):

```protobuf
general {
  operation_mode: MODE_TRAINING
  name: "ant"
}
ai {
  training {
    environment: TRAINING_ENV_SIMULATION
    method: TRAINING_METHOD_EVAL
    simulator_backend: SIM_BACKEND_ISAAC_SIM
    eval {
      task: "ant"
      algorithm: "skrl"
      checkpoint_path: "/tmp/joshua_checkpoints/isaac_logs/ant_isaac_skrl_ppo/final_policy.pt"
      num_episodes: 10
      num_envs: 32
      render: true
    }
  }
}
```

### Supported RL Libraries

| Algorithm | Library | Config value | Default iterations (Ant) |
|-----------|---------|-------------|--------------------------|
| PPO | RSL-RL | `algorithm: "rsl_rl"` | 1000 (32 rollout steps) |
| PPO | skrl | `algorithm: "skrl"` | 500 (16 rollout steps) |

`max_iterations` maps to learning iterations (not raw timesteps).
Each iteration collects `rollout_steps` transitions per env, then
performs one gradient update.

### Joshua's Isaac Lab Task System

Joshua defines custom Isaac Lab tasks in `ai/train/isaac_tasks/`
using a generic term-based architecture:

```
ai/train/isaac_tasks/
├── __init__.py             # auto-registers all tasks on import
├── env_builder.py          # generic ManagerBasedRLEnvCfg builder
├── terms/                  # reusable MDP term functions
│   ├── actions.py          #   action term configs
│   ├── events.py           #   randomization / reset events
│   ├── observations.py     #   observation term factories
│   ├── rewards.py          #   reward term factories
│   └── terminations.py     #   termination conditions
└── tasks/
    └── ant.py              # Ant locomotion (composes generic terms)
```

All terms are **generic** — they reference body/joint names from the
task config, not hardcoded values.  The same reward functions work
for Ant, Humanoid, or any custom robot.

### 3D Models (USD Assets)

Isaac Sim uses USD format.  Joshua stores local assets in
`simulation/models/`:

```
simulation/models/
├── ant.xml                 # MuJoCo XML (for MJX backend)
├── ant_isaac.usda          # USD ASCII (for Isaac Sim backend)
└── ...
```

To create a new robot, write a `.usda` file (or export from a
CAD tool) and place it in `simulation/models/`.  The task file
uses `resolve_model_path()` to locate it at runtime.

### Adding a New Task

1. Create a USD asset in `simulation/models/my_robot.usda`
2. Create `ai/train/isaac_tasks/tasks/my_robot.py` (compose
   generic terms from `terms/`)
3. Register in `ai/train/isaac_tasks/tasks/__init__.py`
4. Add the mapping in `isaac_runner.py` `TASK_MAP`
5. Create a config preset in `config/config_preset/`

### Key Files

| File | Runs in | Purpose |
|------|---------|---------|
| `ai/train/trainer.py` | Joshua (Bazel) | Top-level dispatcher |
| `ai/train/isaac_launcher.py` | Joshua (Bazel) | Config serialization, env sanitization, subprocess |
| `ai/train/isaac_runner.py` | Isaac Lab (venv) | Training/eval bridge, no Joshua imports |
| `ai/train/isaac_tasks/` | Isaac Lab (venv) | Task definitions, terms, env builder |
| `simulation/models/*.usda` | Isaac Lab (venv) | Local USD robot assets |

### Troubleshooting

| Problem | Fix |
|---------|-----|
| `OSError: Isaac Lab not found` | Set `ISAAC_LAB_PATH` or `ISAAC_LAB_PYTHON` env var |
| `Error: Command failed to spawn: Aborted` | Check `nvidia-smi`; verify GPU driver is compatible with your Isaac Sim version |
| Training much slower than native Isaac Lab | Check `max_iterations` — Joshua converts it to `max_iterations * rollout_steps` timesteps (no `num_envs` multiplier) |
| `ModuleNotFoundError: No module named 'pxr'` | This is expected inside Bazel; USD tools only work in the Isaac Lab venv |
