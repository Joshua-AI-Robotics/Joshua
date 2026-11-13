# AGENTS.md — ROS 2

Also follow the root [AGENTS.md](../AGENTS.md); this file adds rules specific to
`ros2/` and its subdirectories. Read [README.md](README.md) before changing code.

## C++ node generation

- Do not add new C/C++ header files (`.h`, `.hh`, `.hpp`, `.hxx`) under `ros2/`.
  Reuse and maintain existing shared headers such as `node_runner.h` and the
  packet, timing, logging, and QoS utilities.
- Keep each node's complete class, implementation, and small runner entrypoint
  in one `<node>.cc` file. Do not create a node header, `<node>_main.cc`, or a
  per-node `main.cc`. Do not move node classes into existing headers as a
  workaround. Follow [camera_publisher.cc](camera_publisher.cc) and
  [position_publisher.cc](position_publisher.cc).
- Delegate startup and execution to the shared node runner:

  ```cpp
  JOSHUA_MAIN(argc, argv) {
    return ros2_utils::RunNode<MyNode>(argc, argv, "my_node");
  }
  ```

- Build production nodes directly with `ros2_cpp_binary` from
  [system_ros2_rules.bzl](../system_ros2_rules.bzl). Keep the system ROS
  dependency handling; do not split a node into a production library and a
  separate entrypoint solely to expose it to tests.
- Keep runtime ownership visible in the node's `.cc`. Reuse existing factories,
  interfaces, packet utilities, and process-runtime helpers. Do not add generic
  node/runtime layers or inherit from another ROS node just to share code.

Use the root [test](../AGENTS.md#test) and [lint](../AGENTS.md#lint) instructions
for validation. This guidance changes generation conventions; existing header
implementations are migrated in focused cleanup PRs.
