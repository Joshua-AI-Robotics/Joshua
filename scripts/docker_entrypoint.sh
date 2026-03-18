#!/bin/bash
# Docker entrypoint script for Joshua containers
# Creates mock serial ports if they don't exist (for development/testing)

set -e

# If ENABLE_MOCK_SERIAL_PORTS is set, create mock serial ports
if [ "${ENABLE_MOCK_SERIAL_PORTS:-false}" = "true" ]; then
    echo "=========================================="
    echo "Creating mock serial ports for development..."
    echo "This allows the application to run without physical serial devices."
    echo "=========================================="
    /workspace/scripts/create_mock_serial_ports.sh --background || true
    sleep 1  # Give socat time to create the ports
    echo "Mock serial ports ready!"
    echo ""
fi

# Execute the command passed to the container, or default to bash for interactive use
if [ $# -eq 0 ]; then
    # No command provided, start an interactive bash shell
    exec /bin/bash
else
    # Execute the provided command
    exec "$@"
fi
