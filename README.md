# JOSHUA (**J**oint **O**pen-**S**ource **H**ub for **U**niversal **A**utomation)
## *A Modular Framework for Robotic AI Systems*

Project Joshua is a user‑friendly, modular framework that turns a single configuration file into a running robot system using ROS2 and Protocol Buffers. A single text config defines your robot hardware (actions and perceptions), AI policy, and operation mode. The system then builds and runs the corresponding ROS2 nodes and lets you monitor/control them from a Qt6 GUI.

## Core Concepts
![Project Joshua Core Concept](assets/images/project_joshua_diagram_napkin.png)

Project Joshua uses ROS2 and Protocol Buffers. A single configuration file is the source of truth for your robot: it declares actions (e.g., number of actuators, actuator type), perceptions (e.g., cameras, encoders), AI policy, and operation mode. For example, a file like [`config/config_preset/so100/so100_teleoperate.pbtxt`](config/config_preset/so100/so100_teleoperate.pbtxt) defines the entire robot and AI system. From this one file, the system instantiates the required ROS2 nodes and runs them to make the robot operational.

The Joshua Control Panel (Qt6 C++ GUI) ties it together: you can create or load the configuration, build required targets, launch the selected preset, and monitor running nodes and their publish/subscribe topics—all from one place.


## Quick Start Example

### Prerequisites

### Option A: Build docker image and run

- **Operating System:** Ubuntu or MAC Apple Silicon
- **Install Docker:** Install docker desktop for your host machine and run. 
- **(Linux Only) Login with gpg key** Docker desktop requires gpg key credentials for linux users.
  Follow the link below to complete gpg key setup. 
  [gpg key link setup](https://docs.docker.com/desktop/setup/sign-in/#credentials-management-for-linux-users)
- **Build Docker Image:** Run the docker command to build the image in need. 
  [Linux Host: ubuntu 22.04 base with ROS2 humble]
  ```bash
  docker compose build joshua-u22
  ```
  If building for ARM 64, build docker image targeted for arm64. 
  ```bash
  docker compose build joshua-u22-arm64
  ```
  [Linux Host: ubuntu 24.04 base with ROS2 jazzy]
  ```bash
  docker compose build joshua-u24
  ```
  arm64 image for joshua-u24 is available as well. 
  [Mac Apple Silicon Host: ubuntu 22.04 base with ROS2 humble]
  ```bash
  docker compose build joshua-mac-u22-arm64
  ```
  Note: Docker image for MAC supports arm64 target build only. Also serial ports are mocked by default. If real serial ports are needed, refer to the comments in docker-compose.yml file for details. 
- **Run interactive shell:**
  [ubuntu 22.04 base with ROS2 humble]
  ```bash
  docker compose run joshua-u22
  ```
  [ubuntu 24.04 base with ROS2 jazzy]
  ```bash
  docker compose run joshua-u24
  ```
  [Mac Host ubuntu 22.04 base with ROS2 humble arm64 target]
  ```bash
  docker compose run joshua-mac-u22-arm64
  ```
  To exit, type exit.
  After exiting, resume the docker container with the container name. 
  To query stopped docker container list, type
  ```bash
  docker container list -a
  ```
  To resume stopped docker, do
  ```bash
  docker start -ai [container_name]
  ```
  For example, the resume command would look like
  ```bash
  docker start -ai joshua-joshua-u22-run-a199afce4b8a
  ```

### Option B: Native Installation
- **Operating System:** Ubuntu 22.04 LTS
- **Setup Script:** Run the automated setup script to install all dependencies:
  ```bash
  sudo ./scripts/setup.sh --env=dev
  ```

### Example: Running SO100 Teleoperation with Follower and Lead Arm

This example demonstrates how to launch the SO100 robot in teleoperation mode, with both the follower and lead arm managed according to your configuration file. The configuration file is predefined at [`config/config_preset/so100/so100_teleoperate.pbtxt`](config/config_preset/so100/so100_teleoperate.pbtxt).

*Please make sure to calibrate the operational limits for your servo motors.*

```bash
bazel run launcher:joshua_main
```

## Key Features & Benefits:

### Simplified Configuration

At the core of Project Joshua's ease of use is its single configuration file approach. A simple text file (e.g., [`so100/so100_teleoperate.pbtxt`](config/config_preset/so100/so100_teleoperate.pbtxt)) is all that's needed to define and orchestrate an entire robotic AI system. This eliminates the need for extensive manual setup and reduces potential for configuration errors.

### Automated System Setup

From a single configuration, Project Joshua automatically handles the creation and setup of:

- **Hardware Interfaces**: Seamlessly integrates with various robotic hardware components, abstracting away low-level complexities.

- **AI Model Policies**: Defines and loads the specific AI models and their operational policies.

- **Parameters**: Manages all necessary system parameters, ensuring consistent behavior across deployments.

- **Tasks**: Configures and initiates the specific robotic tasks the system is designed to perform. (e.g. Inference, Train)

### Modular Architecture

The framework's modular design ensures flexibility and extensibility. New hardware components, AI models, or tasks can be easily integrated without significant refactoring of the core system. This promotes rapid prototyping and iterative development.

### Reduced Learning Curve

By abstracting away the intricacies of hardware-software integration, Project Joshua significantly lowers the barrier to entry for both AI/ML engineers and robotics hardware specialists. This fosters greater collaboration and accelerates the development cycle.

### Accelerated Deployment

The streamlined configuration and automated setup capabilities drastically reduce the time from development to real-world deployment, allowing teams to iterate faster and bring robotic AI solutions to market more efficiently.

### Scalability

Designed with scalability in mind, Project Joshua can accommodate a wide range of robotic systems, from simple prototypes to complex, multi-component deployments.


## Simulation & RL Training

Joshua includes a simulation and reinforcement learning pipeline supporting two physics backends. All modes are accessible through the unified `joshua_main` launcher -- the config's `operation_mode` determines what runs.

### MuJoCo / MJX (Interactive Viewer)

View and interact with MuJoCo models directly:

```bash
bazel run //launcher:joshua_main -- --config config/config_preset/ant/ant_sim_interactive.pbtxt
```

### MJX Training (GPU-Parallel JAX)

Train RL policies on MuJoCo XLA with PPO:

```bash
bazel run //launcher:joshua_main -- --config config/config_preset/ant/ant_train_mjx.pbtxt
```

### Isaac Sim / Isaac Lab Training

Train using NVIDIA Isaac Sim (requires Isaac Lab installed, see `ai/train/README.md`):

```bash
# Set environment variables (add to ~/.bashrc for persistence)
export ISAAC_LAB_PATH=~/IsaacLab
export ISAAC_LAB_PYTHON=~/env_isaaclab/bin/python

# Train Ant with skrl PPO
bazel run //launcher:joshua_main -- --config config/config_preset/ant/ant_train_isaac_full_skrl.pbtxt

# Evaluate a trained checkpoint
bazel run //launcher:joshua_main -- --config config/config_preset/ant/ant_eval_isaac_full_skrl.pbtxt
```

The trainer can also be invoked directly for development:

```bash
bazel run //ai/train:trainer -- --config config/config_preset/ant/ant_train_isaac_full_skrl.pbtxt
```

### Windows Native Isaac Lab Notes

If you are running Joshua from Windows, the Isaac launcher supports a
native Windows Isaac Lab install in addition to the Linux shell path.

Set PowerShell environment variables to your local Isaac install:

```powershell
$env:ISAAC_LAB_PATH = "C:\path\to\IsaacLab"
$env:ISAAC_LAB_PYTHON = "C:\path\to\env_isaaclab\Scripts\python.exe"
$env:OMNI_KIT_ACCEPT_EULA = "YES"
$env:PYTHONPATH = "C:\path\to\Joshua"
```

You can validate simulator startup ("isaacsim running sim only") with:

```powershell
C:\path\to\env_isaaclab\Scripts\python.exe -c "import os; os.environ['OMNI_KIT_ACCEPT_EULA']='YES'; from isaacsim import SimulationApp; app=SimulationApp({'headless': True}); print('SIMULATION_APP_OK'); app.close()"
```

And run a minimal Isaac Lab training smoke test for `Isaac-Ant-v0` with:

```powershell
cmd /c "set OMNI_KIT_ACCEPT_EULA=YES && C:\path\to\env_isaaclab\Scripts\python.exe C:\path\to\IsaacLab\scripts\reinforcement_learning\skrl\train.py --task Isaac-Ant-v0 --num_envs 1 --max_iterations 1 --headless"
```

The subprocess launcher in [`ai/train/isaac_launcher.py`](ai/train/isaac_launcher.py) will:

- use `isaaclab.bat` when `ISAAC_LAB_PATH` points at a Windows Isaac Lab install
- keep using `isaaclab.sh` on non-Windows systems
- still accept an explicit `ISAAC_LAB_PYTHON` override on either platform

Current status on this Windows setup:

- the native Isaac Lab training smoke test for `Isaac-Ant-v0` worked
- the official rendered Isaac Lab `play.py` path was not the validated local playback path on this machine
- the current local playback workaround is to use the MuJoCo-native Ant scripts below

### Available Configs

| Config | Backend | What it does |
|--------|---------|-------------|
| `ant/ant_sim_interactive.pbtxt` | MuJoCo | Interactive 3D viewer |
| `ant/ant_train_isaac_full_skrl.pbtxt` | Isaac Sim | Train Ant (skrl PPO) |
| `ant/ant_train_isaac_full_rsl_rl.pbtxt` | Isaac Sim | Train Ant (RSL-RL PPO) |
| `ant/ant_eval_isaac_full_skrl.pbtxt` | Isaac Sim | Evaluate Ant (skrl) |
| `ant/ant_eval_isaac_full_rsl_rl.pbtxt` | Isaac Sim | Evaluate Ant (RSL-RL) |
| `trileg/trileg_train_isaac_full_skrl.pbtxt` | Isaac Sim | Train 3-legged robot (skrl) |
| `trileg/trileg_train_isaac_full_rsl_rl.pbtxt` | Isaac Sim | Train 3-legged robot (RSL-RL) |
| `trileg/trileg_eval_isaac_full_skrl.pbtxt` | Isaac Sim | Evaluate 3-legged robot (skrl) |
| `trileg/trileg_eval_isaac_full_rsl_rl.pbtxt` | Isaac Sim | Evaluate 3-legged robot (RSL-RL) |
| `so100/so100_teleoperate.pbtxt` | Hardware | SO100 teleoperation |
| `so100/so_arm100_sim_interactive.pbtxt` | MuJoCo | SO-ARM100 interactive sim |

### MuJoCo-native Ant Scripts

For a lightweight local MuJoCo training/playback loop, use the Python
scripts in [`ai/train`](ai/train):

```powershell
$env:PYTHONPATH = "C:\path\to\Joshua"
C:\path\to\env_isaaclab\Scripts\python.exe C:\path\to\Joshua\ai\train\mujoco_ant_train.py --timesteps 100000 --log_dir C:\path\to\Joshua\logs\mujoco_ant --experiment_name ant_walk
```

```powershell
$env:PYTHONPATH = "C:\path\to\Joshua"
C:\path\to\env_isaaclab\Scripts\python.exe C:\path\to\Joshua\ai\train\mujoco_ant_play.py --checkpoint C:\path\to\Joshua\logs\mujoco_ant\ant_walk\final_policy.pt --episodes 3
```

For full documentation on the training pipeline, simulator backends, and how to add new tasks, see [`ai/train/README.md`](ai/train/README.md).


## Data Type Architecture

Project Joshua employs a **dual-layer data type system** that ensures type safety, scalability, and modularity throughout the entire robotic system:

### Configuration Layer (Protobuf)
The system uses **Protocol Buffers (protobuf)** for all configuration and internal data structures:

- **`config.proto`**: Defines the main configuration structure with `General`, `Robot`, and `Ai` components
- **`robot.proto`**: Specifies robot hardware configuration including actions and perceptions
- **`ai.proto`**: Defines AI policy configurations and model parameters
- **`action.proto`**: Configures actuator types, communication interfaces, and operational parameters
- **`perception.proto`**: Defines sensor configurations for cameras, encoders, and other perception devices

### Runtime Data Layer (Protobuf + ROS2)
During runtime, the system uses a **hybrid approach** for optimal performance and compatibility:

#### Internal Communication (Protobuf)
- **`action_packet.proto`**: Structured action commands with support for:
  - Simple commands (position, torque, speed)
  - Preset commands (middle position, idle, teardown)
  - Complex multi-parameter actions
- **`perception_packet.proto`**: Unified perception data format supporting:
  - Image data (width, height, channels, encoding, raw bytes)
  - Position data (position, velocity)
  - Sensor data (multi-value arrays with labels)
  - Point cloud data (for future LiDAR/Radar sensors)

### Data Flow Architecture

```
Hardware Interface → Protobuf Packets → ROS2 Publishers → ROS2 Messages → ROS2 Subscribers → Protobuf Packets → Hardware Interface
```

This architecture provides:
- **Type Safety**: Protobuf ensures data integrity at the hardware interface level
- **Performance**: Efficient serialization/deserialization for internal communication
- **Compatibility**: Standard ROS2 messages enable integration with existing ROS2 ecosystem
- **Extensibility**: Easy addition of new sensor types and action commands
- **Modularity**: Clear separation between configuration, runtime data, and ROS2 communication

## Running the Web UI with Docker

The Joshua Control Panel includes a modern web-based UI that can be run using Docker Compose.

### Prerequisites

- **Docker** and **Docker Compose v2** installed
  ```bash
  # Install Docker and Docker Compose v2
  sudo apt install docker.io docker-compose-plugin
  
  # Or add your user to the docker group to avoid sudo:
  sudo usermod -aG docker $USER
  newgrp docker
  
  # Verify Docker Compose v2 is installed
  docker compose version
  # Should show: Docker Compose version v2.x.x
  ```
  
  **Important:** This project requires **Docker Compose v2** (use `docker compose` without hyphen). 
  - Docker Compose v2 is the modern standard and is included with Docker Desktop and modern Docker installations
  - The legacy v1 (`docker-compose` with hyphen) is not supported
  - If you see "command not found" for `docker compose`, install the plugin:
    ```bash
    sudo apt install docker-compose-plugin
    ```

### Building and Running

From the project root directory:

```bash
# Build and start the UI container (Docker Compose v2)
docker compose up --build

# Or run in detached mode (background)
docker compose up -d --build

# View logs
docker compose logs -f

# Stop the container
docker compose down
```

**For development with Zenoh bridge and demo nodes:**
```bash
# Use the development compose file
docker compose -f docker-compose.yml -f docker-compose.dev.yml up
```

The UI will be available at `http://localhost:3000`

### What It Does

The Docker build process:
1. Builds the React UI application
2. Generates the protobuf schema from your proto files
3. Serves the application using nginx

The build context is set to the project root, so the schema generator can access proto files in `config/`, `robot/`, and `ai/` directories.
