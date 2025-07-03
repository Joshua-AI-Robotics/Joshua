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

### Working with Python Dependencies

This project uses a robust, Bazel-compatible setup for managing Python dependencies to ensure builds are hermetic and reproducible. The system relies on `pip-tools` and a locked set of requirements.

#### Structure Overview

- **`requirements.txt`**: This is a simple, human-readable file where you declare the project's **direct** Python dependencies (e.g., `numpy`, `torch`). You only list the packages you directly `import`.

- **`requirements.lock`**: This is the single source of truth for Bazel. It is an auto-generated file containing the exact versions of _all_ packages (including transitive dependencies). This file is created by `pip-compile` and ensures that every build is identical. **You must commit this file to the repository whenever it changes.**

- **`venv/`**: This project includes a local virtual environment. Its only role in the build process is to provide a consistent environment for running `pip-compile`. It is **not** used by Bazel for building or running code, which uses its own hermetic toolchain. You can, however, activate it for local development and IDE support (e.g., for autocomplete).

#### How to Add or Update a Python Dependency

Follow this two-step process to modify the Python dependencies:

1.  **Edit `requirements.txt`**: Add, remove, or change the version specifier for any top-level package in the `requirements.txt` file.

2.  **Regenerate the Lock File**: Run the following commands from the project root to update `requirements.lock` with your changes and all the necessary transitive dependencies.

    ```bash
    # Activate the local virtual environment
    source venv/bin/activate

    # Re-compile the requirements to update the lock file
    pip-compile --allow-unsafe requirements.txt -o requirements.lock

    # You can now deactivate the venv if you wish
    deactivate
    ```

3.  **Commit Your Changes**: Add both the modified `requirements.txt` and the newly generated `requirements.lock` to your git commit.
