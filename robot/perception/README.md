# Perception

Sensor drivers expose packets through PerceptionInterface and are selected by
PerceptionFactory. Cameras, audio, lidar, position feedback, and board scalar
sensors retain their own I/O and packet semantics. The [contract](AGENTS.md)
owns initialization, read/cancellation, metadata, and verification requirements.
ROS scheduling and publication belong in [ros2/](../../ros2/AGENTS.md).

Production drivers can open real sensors and buses. Follow the root
[hardware authorization rules](../../AGENTS.md#hardware-safety--read-this-first).
