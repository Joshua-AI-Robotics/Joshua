# Project Joshua

Project Joshua is a flexible and modular C++ framework for building and controlling robotic systems. It is designed around a central "Nexus" architecture that orchestrates the flow of data from perception sensors to an AI decision-making layer, and finally to motor actuators.

## Core Concepts

The architecture is centered around the **Nexus**, which acts as the robot's central nervous system. It operates on a simple yet powerful loop:

1.  **Perception**: Gathers data from all registered sensors (like cameras).
2.  **Decision Making**: Packages the sensor data and sends it to an AI layer for processing.
3.  **Action**: Receives action commands from the AI layer and dispatches them to the appropriate motors or actuators.

This decoupled design allows for modular development, where new sensors, actuators, and AI models can be integrated with minimal changes to the core system. For a more detailed explanation, see the `README.md` in `robot/nexus/`.

## Technology Stack

- **C++**: The primary language for performance-critical robotics applications.
- **Bazel**: The build system used for managing dependencies and ensuring reproducible builds.
- **Protobuf**: Used for serializing structured data for communication between different parts of the system.
- **glog / gflags**: For robust logging and command-line argument parsing.
- **OpenCV**: For camera and computer vision-related tasks.

## Project Structure

The project is organized into the following main directories:

- `robot/`: Contains the core logic for the robot's operation.
  - `nexus/`: The central hub that orchestrates perception and action.
  - `perception/`: Interfaces and drivers for sensors.
  - `actuation/`: Interfaces and drivers for motors and actuators.
  - `comm_interface/`: Low-level communication protocols (e.g., Serial).
  - `config/`: Configuration files for the robot.
- `ai/`: Intended for the AI and machine learning models that will power the robot's decisions. (Currently a work in progress).
- `utils/`: Contains various utility programs, controllers, and scripts for testing and debugging.

## Getting Started

### Prerequisites

- A C++ compiler (supporting C++14 or later)
- [Bazel](https://bazel.build/install) build system

### Building the Project

To build all targets, run the following command from the project's root directory:

```bash
bazel build //...
```

### Running the Main Application

To run the primary Nexus application, which starts the robot's main control loop, use the following command:

```bash
bazel run //:nexus_main
```
