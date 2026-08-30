#!/bin/bash
# Container entrypoint for Joshua images.
#
# /etc/bash.bashrc only sources ROS for interactive shells, so one-shot
# commands like `docker compose run --rm run-u22` would otherwise start with
# an empty AMENT_PREFIX_PATH and fail to import rclpy (librcl_action.so).
# Source the ROS 2 environment here so every container command sees it.

set -e

if [ -n "${ROS_DISTRO:-}" ] && [ -f "/opt/ros/${ROS_DISTRO}/setup.bash" ]; then
    source "/opt/ros/${ROS_DISTRO}/setup.bash"
else
    echo "WARNING: /opt/ros/${ROS_DISTRO:-<ROS_DISTRO unset>}/setup.bash not found;" \
         "ROS 2 environment NOT sourced. ROS commands will fail with an empty AMENT_PREFIX_PATH." >&2
fi

# Optionally create mock serial ports (macOS/ARM64 development without
# physical serial devices).
if [ "${ENABLE_MOCK_SERIAL_PORTS:-false}" = "true" ]; then
    echo "Creating mock serial ports for development..."
    /workspace/scripts/create_mock_serial_ports.sh --background || true
    sleep 1  # Give socat time to create the ports
fi

if [ $# -eq 0 ]; then
    exec /bin/bash
else
    exec "$@"
fi
