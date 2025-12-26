# JOSHUA (**J**oint **O**pen-**S**ource **H**ub for **U**niversal **A**utomation)
## *A Modular Framework for Robotic AI Systems*

Project Joshua is a user‑friendly, modular framework that turns a single configuration file into a running robot system using ROS2 and Protocol Buffers. A single text config defines your robot hardware (actions and perceptions), AI policy, and operation mode. The system then builds and runs the corresponding ROS2 nodes and lets you monitor/control them from a Qt6 GUI.

## Core Concepts
![Project Joshua Core Concept](assets/images/project_joshua_diagram_napkin.png)

Project Joshua uses ROS2 and Protocol Buffers. A single configuration file is the source of truth for your robot: it declares actions (e.g., number of actuators, actuator type), perceptions (e.g., cameras, encoders), AI policy, and operation mode. For example, a file like [`config/config_preset/so100_mock_inference.pbtxt`](config/config_preset/so100_mock_inference.pbtxt) defines the entire robot and AI system. From this one file, the system instantiates the required ROS2 nodes and runs them to make the robot operational.

The Joshua Control Panel (Qt6 C++ GUI) ties it together: you can create or load the configuration, build required targets, launch the selected preset, and monitor running nodes and their publish/subscribe topics—all from one place.


## Quick Start Example

### Prerequisites

### Option A: Build docker image and run

- **Operating System:** Ubuntu 22.04 LTS
- **Install Docker:** Install docker with the following command
  ```bash
  sudo apt install docker.io
  ```
- **Build Docker Image:** Run the docker command to build the image in need. 
  [ubuntu 22.04 base with ROS2 humble]
  ```bash
  docker compose build joshua-u22
  ```
  If building for ARM 64, build docker image targeted for arm64. 
  ```bash
  docker compose build joshua-u22-arm64
  ```
  [ubuntu 24.04 base with ROS2 jazzy]
  ```bash
  docker compose build joshua-u24
  ```
  arm64 image for joshua-u24 is available as well. 
- **Run interactive shell:**
  [ubuntu 22.04 base with ROS2 humble]
  ```bash
  docker compose run joshua-u22
  ```
  [ubuntu 24.04 base with ROS2 jazzy]
  ```bash
  docker compose run joshua-u24
  ```
  To exit, type exit.
  After exiting, resume the docker container with:
  ```bash
  docker start -i joshua-u22
  ```
  or 
  ```bash
  docker start -i jushua-u24
  ```

### Option B: Native Installation
- **Operating System:** Ubuntu 22.04 LTS
- **Setup Script:** Run the automated setup script to install all dependencies:
  ```bash
  sudo ./scripts/setup.sh --env=dev
  ```

### Example: Running SO100 Teleoperation with Follower and Lead Arm

This example demonstrates how to launch the SO100 robot in teleoperation mode, with both the follower and lead arm managed according to your configuration file. The configuration file is predefined at [`config/config_preset/so100_teleoperate.pbtxt`](config/config_preset/so100_teleoperate.pbtxt).

*Please make sure to calibrate the operational limits for your servo motors.*

```bash
bazel run launcher:joshua_main
```

## Key Features & Benefits:

### Simplified Configuration

At the core of Project Joshua's ease of use is its single configuration file approach. A simple text file (e.g., [so100_with_example_ai.pbtxt](`config/config_preset/so100_with_example_ai.pbtxt`)) is all that's needed to define and orchestrate an entire robotic AI system. This eliminates the need for extensive manual setup and reduces potential for configuration errors.

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

- **Docker** and **Docker Compose** installed
  ```bash
  sudo apt install docker.io docker-compose
  # Or add your user to the docker group to avoid sudo:
  sudo usermod -aG docker $USER
  newgrp docker
  ```

### Building and Running

From the project root directory:

```bash
# Build and start the UI container
docker-compose up --build

# Or run in detached mode (background)
docker-compose up -d --build

# View logs
docker-compose logs -f

# Stop the container
docker-compose down
```

The UI will be available at `http://localhost:3000`

### What It Does

The Docker build process:
1. Builds the React UI application
2. Generates the protobuf schema from your proto files
3. Serves the application using nginx

The build context is set to the project root, so the schema generator can access proto files in `config/`, `robot/`, and `ai/` directories.
