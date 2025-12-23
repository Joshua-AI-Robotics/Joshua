#!/bin/bash
set -e

# Joshua Project - Runtime Environment Setup Script
# Use this script on a NEW robot/machine (e.g. Raspberry Pi) to install
# necessary dependencies to run the built binaries.

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Joshua Project Runtime Setup ===${NC}"

# 1. Detect OS and determine ROS Distro
if [ -f /etc/os-release ]; then
    . /etc/os-release
    UBUNTU_CODENAME=$UBUNTU_CODENAME
    VERSION_ID=$VERSION_ID
else
    echo -e "${RED}Error: Cannot detect OS version. Is this Ubuntu?${NC}"
    exit 1
fi

echo -e "${YELLOW}Detected OS: Ubuntu $VERSION_ID ($UBUNTU_CODENAME)${NC}"

ROS_DISTRO=""
if [ "$VERSION_ID" == "22.04" ]; then
    ROS_DISTRO="humble"
elif [ "$VERSION_ID" == "24.04" ]; then
    ROS_DISTRO="jazzy"
else
    echo -e "${RED}Error: Unsupported Ubuntu version: $VERSION_ID. Only 22.04 (Humble) and 24.04 (Jazzy) are supported.${NC}"
    exit 1
fi

echo -e "${GREEN}Target ROS 2 Distro: $ROS_DISTRO${NC}"

# 2. Setup ROS 2 Repositories
echo -e "${YELLOW}Setting up ROS 2 repositories...${NC}"
sudo apt-get update
sudo apt-get install -y software-properties-common curl locales

# Ensure locale
sudo locale-gen en_US en_US.UTF-8
sudo update-locale LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8
export LANG=en_US.UTF-8

sudo add-apt-repository universe -y
sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -o /usr/share/keyrings/ros-archive-keyring.gpg

echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu $UBUNTU_CODENAME main" | sudo tee /etc/apt/sources.list.d/ros2.list > /dev/null

# 3. Install Dependencies
echo -e "${YELLOW}Installing dependencies...${NC}"
sudo apt-get update

# Install OpenCV (Essential for perception nodes)
echo "Installing OpenCV..."
sudo apt-get install -y libopencv-dev

# Install ROS 2 Base
echo "Installing ROS 2 ($ROS_DISTRO)..."
sudo apt-get install -y ros-$ROS_DISTRO-ros-base

# Install essential message packages (fixes "missing libstd_srvs...so" errors)
echo "Installing ROS 2 Interface packages..."
sudo apt-get install -y \
    ros-$ROS_DISTRO-std-msgs \
    ros-$ROS_DISTRO-sensor-msgs \
    ros-$ROS_DISTRO-geometry-msgs \
    ros-$ROS_DISTRO-std-srvs \
    ros-$ROS_DISTRO-nav-msgs

# Install Jazzy-specific packages if needed
if [ "$ROS_DISTRO" == "jazzy" ]; then
    echo "Installing Jazzy-specific packages..."
    sudo apt-get install -y \
        ros-$ROS_DISTRO-service-msgs \
        ros-$ROS_DISTRO-type-description-interfaces \
        ros-$ROS_DISTRO-rosidl-dynamic-typesupport
fi

# 4. Configure Environment
echo -e "${YELLOW}Configuring environment...${NC}"

SETUP_CMD="source /opt/ros/$ROS_DISTRO/setup.bash"

# Check if already in bashrc
if grep -q "$SETUP_CMD" ~/.bashrc; then
    echo "ROS 2 setup already in .bashrc"
else
    echo "Adding ROS 2 setup to .bashrc..."
    echo "" >> ~/.bashrc
    echo "# ROS 2 $ROS_DISTRO Setup" >> ~/.bashrc
    echo "$SETUP_CMD" >> ~/.bashrc
fi

# Also ensure LD_LIBRARY_PATH includes ROS lib (sometimes needed for direct binary execution)
# But strictly speaking, the setup.bash handles this.

echo -e "${GREEN}=== Setup Complete! ===${NC}"
echo -e "Please run the following command to apply changes to your current shell:"
echo -e "${YELLOW}source ~/.bashrc${NC}"

