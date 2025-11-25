# Node Generator

The `Node Generator` is a core component of Joshua platform, responsible for dynamically instantiating and managing ROS2 nodes based on a configuration file. It acts as an orchestrator, parsing the user-defined robot configuration, launching the nodes, and monitoring their lifecycle.

## Workflow

The Node Generator follows a clear, sequential process to bring the robotic system to life, as illustrated in the diagram above:

1.  **Parse the Config**: The process begins by reading a `.pbtxt` configuration file specified via the `--config` flag. This file contains all the necessary information about the robot's hardware, sensors, actuators, and AI policies.

2.  **Analyze Nodes and Interfaces**: The generator analyzes the configuration to identify all the required node types. It groups these components by their assigned `node_id` and ensures no duplicates or conflicts (e.g., multiple nodes claiming the same serial port).

3.  **Integrity Check for Config**: It performs a series of checks to ensure the configuration is valid and all required information is present. This step prevents runtime errors due to misconfiguration.

4.  **Deploy/Run All Nodes**: Once the analysis is complete, the generator launches each node as a separate process using `fork()` and `execv()`. It intelligently resolves the binary path (checking Bazel's bin tree or packaged wrapper scripts) and sets up the necessary environment variables (e.g., `RUNFILES_DIR`) for each node.

5.  **Monitor Running Nodes**: After launching, the Node Generator continuously monitors the status of all child processes. If a node terminates unexpectedly, it logs the event. It also handles graceful shutdown, sending `SIGINT` (and subsequently `SIGTERM`/`SIGKILL` if needed) to all child process groups when the main program exits.

## Usage

The Node Generator is executed from the command line, pointing to a specific configuration file.

### Running the Node Generator

Check the [/launcher/joshua_main.cc](/launcher/joshua_main.cc)
