# AGENTS.md — ROS 2

Also follow the root [AGENTS.md](../AGENTS.md) and
[coding guidelines](../docs/CODING_GUIDELINES.md). Read [README.md](README.md)
for the existing runtime and message contracts. These rules apply throughout
`ros2/`.

## One source file per component node

- Each C++ component node must have one canonical `.cc` file containing its
  node class, component-specific configuration, callbacks, lifecycle behavior,
  and binary entrypoint. Examples are [actuator_subscriber.cc](actuator_subscriber.cc),
  [position_publisher.cc](position_publisher.cc), [camera_publisher.cc](camera_publisher.cc),
  and [lidar_publisher.cc](lidar_publisher.cc). These filenames identify where
  each component's behavior belongs; they are not a claim that every existing
  file already follows this layout.
- That file must directly include `ros2/node_runner.h` and define
  `JOSHUA_MAIN(argc, argv)` calling `ros2_utils::RunNode<ComponentNode>(...)`.
  Its Bazel binary target must compile that `.cc` as its entrypoint.
- Keep the complete node implementation in that file. Do not split it into a
  node header plus a forwarding main, or make it a thin entrypoint for a generic
  node that owns the actual sensor or actuator behavior.
- Shared conversions and focused utilities may remain separate libraries.
  Hardware drivers remain in `robot/`, and tests remain separate files.
  Reuse must preserve a clear owner for each component's ROS behavior.
- Use [node_runner.h](node_runner.h) for common process initialization,
  validation, executor setup, and cleanup. Keep component setup, callbacks, and
  teardown visible in the component's `.cc`; do not add another runner or
  lifecycle owner around them.
- A node may own multiple configured devices or share a bus as its contract
  requires. One source file per component does not require one process per
  physical device or duplicate ownership of a shared bus.
