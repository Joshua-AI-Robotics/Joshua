#!/bin/bash
# Script to create mock serial ports for development/testing
# This is useful when running in Docker without physical serial devices
# Creates PTY pairs using socat that can be used as serial ports

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Ports to create (from common config files)
PORTS=("/dev/ttyACM0" "/dev/ttyACM1" "/dev/ttyUSB0")

# Function to check if port exists
port_exists() {
    [ -e "$1" ]
}

# Function to create mock serial port using socat
create_mock_port() {
    local port=$1
    
    if port_exists "$port"; then
        echo -e "${YELLOW}Port $port already exists, skipping...${NC}"
        return 0
    fi
    
    echo -e "${GREEN}Creating mock serial port: $port${NC}"
    
    # Check if /dev is mounted from host (macOS)
    # If /dev is mounted, we need to create the port differently
    if mountpoint -q /dev 2>/dev/null; then
        echo -e "${YELLOW}Warning: /dev appears to be mounted from host.${NC}"
        echo -e "${YELLOW}Creating PTY and attempting to create symlink...${NC}"
    fi
    
    # Create PTY pair using socat
    # This creates a bidirectional pipe that looks like a serial port
    # The link= option creates a symlink to the PTY
    # Use -d -d for minimal output, redirect stderr to avoid noise
    socat -d -d pty,raw,echo=0,link="$port" pty,raw,echo=0 2>/dev/null &
    local socat_pid=$!
    
    # Wait a moment for the symlink to be created
    sleep 1
    
    if port_exists "$port"; then
        echo -e "${GREEN}Successfully created $port (socat PID: $socat_pid)${NC}"
        # Store PID for cleanup
        echo "$socat_pid" >> /tmp/mock_serial_pids.txt
        return 0
    else
        # If symlink creation failed (e.g., /dev is mounted from host),
        # try creating it manually if we have permissions
        echo -e "${YELLOW}Symlink creation may have failed. Checking PTY...${NC}"
        # Find the PTY that socat created
        local pty=$(ps -p $socat_pid -o args= | grep -o '/dev/pts/[0-9]*' | head -1)
        if [ -n "$pty" ] && [ -e "$pty" ]; then
            echo -e "${YELLOW}Found PTY: $pty, attempting manual symlink...${NC}"
            # Try to create symlink manually (may fail if /dev is mounted read-only or from host)
            ln -sf "$pty" "$port" 2>/dev/null && echo -e "${GREEN}Manual symlink created${NC}" || \
                echo -e "${RED}Could not create symlink. Port may not be accessible at $port${NC}"
        fi
        # Store PID anyway for cleanup
        echo "$socat_pid" >> /tmp/mock_serial_pids.txt
        return 1
    fi
}

# Main execution
echo -e "${GREEN}Setting up mock serial ports for development...${NC}"

# Check if socat is available
if ! command -v socat &> /dev/null; then
    echo -e "${RED}Error: socat is not installed. Please install it first.${NC}"
    echo "  apt-get update && apt-get install -y socat"
    exit 1
fi

# Create PID file for cleanup
rm -f /tmp/mock_serial_pids.txt
touch /tmp/mock_serial_pids.txt

# Create each port
for port in "${PORTS[@]}"; do
    create_mock_port "$port" || true
done

echo -e "${GREEN}Mock serial ports setup complete!${NC}"
echo -e "${YELLOW}Note: These are virtual ports for development.${NC}"
echo -e "${YELLOW}To clean up, run: pkill -f 'socat.*tty'${NC}"

# In background mode, just exit (socat processes will keep running)
# The ports will persist as long as socat processes run
if [ "$1" = "--background" ]; then
    exit 0
fi

# Keep script running to maintain the ports
echo -e "${GREEN}Press Ctrl+C to stop and clean up...${NC}"
trap "echo 'Cleaning up...'; pkill -f 'socat.*tty' 2>/dev/null; exit" INT TERM
# Keep running
while true; do sleep 1; done
