# Packet parser

`//ros2/utils:packet_parser` is a C++ library for translating between native ROS
values and Joshua's internal `ActionPacket` / `PerceptionPacket` protobufs.

- `ActionPacketFromFloat` selects position, speed, torque, or dc from the topic
  suffix `/<device>/<action>`.
- `DenormalizeActionPacket` maps normalized positions (including complex action
  positions) to configured actuator limits.
- `RequirePerceptionPosition`, `RequirePerceptionImage`, and
  `RequirePerceptionPointCloud` validate sensor packets before publishing.

Errors return `absl::Status` / `absl::StatusOr`; callers should log and skip invalid
messages. Hardware drivers consume the resulting packets and enforce device limits.

The Python packet parser has been removed. Inference performs its small scalar
normalization calculation in `ai/inference/host.py`; the C++ trajectory publisher
extracts scalar waypoint values directly.

When changing packet protos, update the topic suffix allowlist, action conversions,
position normalization, perception checks, and tests together. New trajectory
scalar actions also require updating `TrajectoryPublisher::ScalarValue`.

The C++ `//ros2/utils:packet_parser_test` covers these conversions and the packet
field contracts. Run the full Docker task suite with `docker compose run --rm
test-u22` or `test-u24`.
