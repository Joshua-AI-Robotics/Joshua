#!/bin/bash
# Joshua Project Setup Script
# Installs all prerequisites for Project Joshua

set -e  # Exit on error

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to check Ubuntu version
check_ubuntu_version() {
    echo -e "${BLUE}Checking Ubuntu version...${NC}"
    if ! grep -q "22.04" /etc/os-release; then
        echo -e "${RED}Error: This script is designed for Ubuntu 22.04 LTS${NC}"
        echo -e "${RED}Please install Ubuntu 22.04 LTS and run this script again${NC}"
        exit 1
    fi
    echo -e "${GREEN}Ubuntu 22.04 LTS detected${NC}"
}

# Function to update package lists
update_packages() {
    echo -e "${BLUE}Updating package lists...${NC}"
    sudo apt-get update
}

# Function to install ROS2
install_ros2() {
    echo -e "${BLUE}Installing ROS2...${NC}"
    sudo apt-get install -y ros-humble-desktop
    sudo apt-get install -y python3-rosdep python3-rosinstall python3-rosinstall-generator python3-wstool build-essential
    sudo apt-get install -y python3-colcon-common-extensions

    # Initialize rosdep
    echo -e "${BLUE}Initializing rosdep...${NC}"
    sudo rosdep init || true  # Ignore error if already initialized
    rosdep update
}

# Function to install Qt6
install_qt6() {
    echo -e "${BLUE}Installing Qt6 development packages...${NC}"
    sudo apt install -y qt6-base-dev qt6-base-dev-tools qt6-tools-dev qt6-tools-dev-tools
}

# Function to install OpenCV
install_opencv() {
    echo -e "${BLUE}Installing OpenCV (both x86_64 and ARM64)...${NC}"
    sudo apt-get install -y libopencv-dev

    # Install ARM64 OpenCV libraries
    echo -e "${BLUE}Installing OpenCV ARM64 libraries...${NC}"
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    sudo "$SCRIPT_DIR/install_opencv_arm64.sh"
}

# Function to install ARM64 cross-compilation tools
install_arm64_tools() {
    echo -e "${BLUE}Installing ARM64 cross-compilation tools (minimal - LLVM toolchain handles most compilation)...${NC}"
    sudo apt-get install -y g++-aarch64-linux-gnu
}

# Function to install Bazel
install_bazel() {
    echo -e "${BLUE}Installing Bazel...${NC}"
    if ! command -v bazel &> /dev/null; then
        # Install Bazel using the official installer
        curl -fsSL https://bazel.build/bazel-release.pub.gpg | gpg --dearmor > bazel-archive-keyring.gpg
        sudo mv bazel-archive-keyring.gpg /usr/share/keyrings
        echo "deb [arch=amd64 signed-by=/usr/share/keyrings/bazel-archive-keyring.gpg] https://storage.googleapis.com/bazel-apt stable jdk1.8" | sudo tee /etc/apt/sources.list.d/bazel.list
        sudo apt-get update
        sudo apt-get install -y bazel
    else
        echo -e "${GREEN}Bazel is already installed${NC}"
    fi
}

# Function to setup user permissions
setup_user_permissions() {
    echo -e "${BLUE}Setting up user permissions...${NC}"

    # Add user to dialout group for hardware access
    echo -e "${BLUE}Adding user to dialout group for UART and GPIO access...${NC}"
    sudo usermod -aG dialout $USER

    # Add user to video group for camera access
    echo -e "${BLUE}Adding user to video group for camera access...${NC}"
    sudo usermod -aG video $USER
}

# Function to setup ROS2 environment
setup_ros2_environment() {
    echo -e "${BLUE}Setting up ROS2 environment...${NC}"
    if ! grep -q "source /opt/ros/humble/setup.bash" ~/.bashrc; then
        echo "source /opt/ros/humble/setup.bash" >> ~/.bashrc
        echo -e "${GREEN}Added ROS2 setup to ~/.bashrc${NC}"
    else
        echo -e "${GREEN}ROS2 setup already in ~/.bashrc${NC}"
    fi
}

# Main execution
main() {
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}  JOSHUA Project Setup Script${NC}"
    echo -e "${GREEN}  Installing prerequisites...${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo

    check_ubuntu_version
    update_packages
    install_ros2
    install_qt6
    install_opencv
    install_arm64_tools
    install_bazel
    setup_user_permissions
    setup_ros2_environment
    
    echo
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}  Setup Complete!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo
    echo -e "${YELLOW}Important: You need to reboot for group changes to take effect${NC}"
}

# Execute main function
main "$@"