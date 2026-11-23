# Xbox controller tool

Follow the repository [instructions](../../../AGENTS.md) and
[tools contract](../../README.md). This standalone operator-input tool is
independent of Joshua's configured robot drivers and launcher graph.

## Architecture contract

`XboxController` owns the Linux evdev descriptor and libevdev handle; its
destructor releases both. `XboxControllerState` holds the raw input state.
`XboxControllerPublisher` owns the controller, state, ROS publisher and RobotTime
timer. Its executor and ROS context belong to the executable's entrypoint.
The controller library is private to this Bazel package. Preserve its existing
`utils` namespace during this relocation; shared system utilities remain in
the repository's top-level `utils/` directory.

The data path is evdev → controller state → ROS publication. This package
contains no servo mappings, motor commands, board connections or independent
recording pipeline. Robot-specific consumers use ROS topics and protobuf setup.

## Implementation rules

Keep this package standalone: production nodes and launcher code must not depend
on its private input library. Reuse the shared RobotTime timer, runtime guard and
ROS logging adapters. The blocking `Run()` API's 10 ms `select()` timeout is an
OS elapsed I/O wait, independent of RobotTime pauses; never publish it as a
robot timestamp.

Preserve the node name, topic, message type, array ordering, scaling and clock
behavior documented in [README.md](README.md#ros-interface). Device-selection,
initialization, reconnect or disconnect changes require focused fake-input tests;
do not infer physical readiness from compilation. Keep robot-specific command
mapping in consumers, using existing ROS interfaces and protobuf setup.

## Contents

- [xbox_controller.h](xbox_controller.h) and
  [xbox_controller.cc](xbox_controller.cc) retain the existing device discovery
  and raw event processing.
- [xbox_controller_publisher.cc](xbox_controller_publisher.cc) owns the ROS adapter
  and executable entrypoint.
- [BUILD](BUILD) owns the private library and standalone executable.
- [README.md](README.md) owns operating commands, the ROS interface and
  compatibility limits.

## Usage

Use the [README build and run instructions](README.md#build). Running the
publisher reads physical input devices; follow the root
[hardware rules](../../../AGENTS.md#hardware-safety--read-this-first).

## Verification

Build the executable in both Humble and Jazzy using their matching Bazel and
Python flags. Run the canonical test services from the root instructions for
integration coverage. Build and test commands must not launch the controller
publisher or require a connected controller. No controller-specific automated
tests exist in this baseline; compilation does not qualify physical input,
disconnect handling, or downstream robot behavior.

For documentation changes, run `./hooks/agents_doc_check.sh`,
`./hooks/skills_doc_check.sh` and check Markdown links. Run
`./hooks/lint_check.sh --fix`, `buildifier -mode=check
tools/teleop/xbox_controller/BUILD` and `git diff --check` before submitting.
