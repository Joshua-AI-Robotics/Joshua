# AGENTS.md — Robot

Also follow the root [AGENTS.md](../AGENTS.md) and
[coding guidelines](../docs/CODING_GUIDELINES.md). Read [README.md](README.md)
for the existing layer responsibilities and resource contracts. These rules
apply throughout `robot/`.

## Modular implementations through factories

- Extend action, perception, board, and communication capabilities as parallel
  sibling implementations behind their existing interfaces. Adding a camera,
  lidar, motor, board, or transport should leave unrelated implementations
  independently understandable and changeable.
- Select and assemble implementations through the existing `ActionFactory`,
  `PerceptionFactory`, `BoardFactory`, and `CommFactory`. Keep concrete selection,
  construction checks, and resource resolution in the responsible factory.
  Update its existing registration table and protobuf declaration when needed;
  do not introduce another factory hierarchy or configuration format.
- Consumers depend on interfaces and capabilities, not concrete drivers or
  vendor/transport switches. Motor semantics belong in action drivers, sensing
  and acquisition in perception drivers, board protocol/session behavior in
  board implementations, and byte movement in communication implementations.
  Keep these choices independently configurable.
- Keep factories focused on construction and assembly. Do not turn them into
  generic runtime orchestrators. Device-specific operation, recovery, and
  teardown stay with the responsible implementation; reusable helpers stay
  within the existing interface boundaries.
- Preserve the shared board and transport ownership established by the
  factories. New consumers must reuse the existing owner instead of opening
  the same bus independently or adding a competing reader, coordinator, or
  lifecycle manager.
- Keep this layer C++ and independent of ROS 2 nodes and message types. ROS
  publication, services, executors, and process entrypoints belong in `ros2/`.
  Keep hardware values in protobuf configuration and test doubles in test-only
  targets. See [comm/README.md](comm/README.md) for transport capability rules.
