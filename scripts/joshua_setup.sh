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

# Check if running with sudo
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run with sudo${NC}"
    echo "Usage: sudo ./scripts/joshua_setup.sh"
    exit 1
fi

# Check the script running directory. This script should be run from the root of the repository.
if [ "$(pwd)" != "$(git rev-parse --show-toplevel)" ]; then
    echo -e "${RED}Error: This script must be run from the root of the repository${NC}"
    echo "Usage: cd $(git rev-parse --show-toplevel) && sudo ./scripts/joshua_setup.sh"
    exit 1
fi

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

# Function to ensure Git is installed
install_git() {
    echo -e "${BLUE}Ensuring Git is installed...${NC}"
    if ! command -v git &> /dev/null; then
        sudo apt-get install -y git
    else
        echo -e "${GREEN}Git is already installed${NC}"
    fi

    # Ensure Git LFS is installed
    if ! command -v git-lfs &> /dev/null; then
        echo -e "${BLUE}Installing Git LFS...${NC}"
        sudo apt-get install -y git-lfs
    fi
}

# Function to install ARM64 cross-compilation tools
install_arm64_tools() {
    echo -e "${BLUE}Installing ARM64 cross-compilation tools (minimal - LLVM toolchain handles most compilation)...${NC}"
    sudo apt-get install -y g++-aarch64-linux-gnu
}

# Function to install Bazel
install_bazel() {
    echo -e "${BLUE}Checking for Bazel...${NC}"
    
    # Check for .bazelversion file to determine required version
    local required_version=""
    if [ -f ".bazelversion" ]; then
        required_version=$(cat .bazelversion | tr -d '[:space:]')
    fi

    if command -v bazel &> /dev/null; then
        if [ -n "$required_version" ]; then
            # Check if installed version matches required version
            local current_version=$(bazel --version 2>/dev/null | head -n 1 | awk '{print $2}' | tr -d '[:space:]')
            
            if [[ "$current_version" == "$required_version" ]]; then
                echo -e "${GREEN}Bazel $required_version is already installed${NC}"
                return 0
            else
                echo -e "${YELLOW}Version mismatch: Found '$current_version', expected '$required_version'. Reinstalling Bazelisk...${NC}"
            fi
        else
            echo -e "${GREEN}Bazel is already installed${NC}"
            return 0
        fi
    fi

    # Detect architecture
    ARCH=$(uname -m)
    case "$ARCH" in
        x86_64)
            BAZEL_ARCH="amd64"
            ;;
        aarch64|arm64)
            BAZEL_ARCH="arm64"
            ;;
        *)
            echo -e "${RED}Unsupported architecture: $ARCH${NC}"
            return 1
            ;;
    esac

    echo -e "${BLUE}Detected $ARCH. Installing Bazelisk for linux-$BAZEL_ARCH...${NC}"
    
    local url="https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-$BAZEL_ARCH"
    
    # Download directly to destination using curl or wget
    if command -v curl &> /dev/null; then
        if ! curl -L "$url" | sudo tee /usr/local/bin/bazel > /dev/null; then
             echo -e "${RED}Failed to download Bazel using curl${NC}"
             return 1
        fi
    elif command -v wget &> /dev/null; then
        if ! wget -qO- "$url" | sudo tee /usr/local/bin/bazel > /dev/null; then
             echo -e "${RED}Failed to download Bazel using wget${NC}"
             return 1
        fi
    else
        echo -e "${RED}Error: Neither curl nor wget found. Please install one of them.${NC}"
        return 1
    fi

    sudo chmod +x /usr/local/bin/bazel
    
    if command -v bazel &> /dev/null; then
        echo -e "${GREEN}Bazel installed successfully!${NC}"
    else
        echo -e "${RED}Bazel installation failed.${NC}"
        return 1
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

# Install linting tools
install_linting_tools() {
    echo -e "${BLUE}Installing linting tools...${NC}"
    sudo apt-get install -y clang-format

    # Determine buildifier URL based on architecture
    ARCH=$(uname -m)
    case "$ARCH" in
        x86_64)
            BUILDIFIER_URL="https://github.com/bazelbuild/buildtools/releases/download/v6.4.0/buildifier-linux-amd64"
            ;;
        aarch64|arm64)
            BUILDIFIER_URL="https://github.com/bazelbuild/buildtools/releases/download/v6.4.0/buildifier-linux-arm64"
            ;;
        *)
            echo -e "${YELLOW}Unknown architecture ($ARCH). Skipping buildifier install.${NC}"
            BUILDIFIER_URL=""
            ;;
    esac

    if [ -n "$BUILDIFIER_URL" ]; then
        # If a directory was mistakenly created at the target path, remove it
        if [ -d "/usr/local/bin/buildifier" ]; then
            echo -e "${YELLOW}/usr/local/bin/buildifier exists as a directory; removing it...${NC}"
            sudo rm -rf /usr/local/bin/buildifier
        fi

        # Download and install directly to avoid temp dir permission issues
        curl -L "$BUILDIFIER_URL" | sudo tee /usr/local/bin/buildifier >/dev/null
        sudo chmod 0755 /usr/local/bin/buildifier
        buildifier --version || true
    fi
}

# Ensure Python pip is available (needed for installing pre-commit for the user)
ensure_python_tooling() {
    echo -e "${BLUE}Ensuring Python pip is installed...${NC}"
    if ! python3 -m pip --version >/dev/null 2>&1; then
        sudo apt-get update || true
        sudo apt-get install -y python3-pip python3-venv || true
        python3 -m ensurepip --upgrade >/dev/null 2>&1 || true
        python3 -m pip install --upgrade pip || true
    fi
}

# Install pre-commit for the original user and enable hooks
install_precommit_and_hooks() {
    echo -e "${BLUE}Configuring pre-commit hooks...${NC}"

    # Original invoking user (not root)
    NONROOT_USER=${SUDO_USER:-$USER}
    if [ -z "$NONROOT_USER" ] || [ "$NONROOT_USER" = "root" ]; then
        echo -e "${YELLOW}Could not determine non-root user. Skipping pre-commit hook installation.${NC}"
        return 0
    fi

    # Ensure pre-commit is installed for the non-root user
    sudo -u "$NONROOT_USER" -H bash -lc 'python3 -m pip install --user --upgrade pre-commit'

    # Install Git LFS for the repository (skip if not available)
    REPO_DIR="$(pwd)"
    if command -v git-lfs >/dev/null 2>&1; then
        sudo -u "$NONROOT_USER" -H bash -lc "cd \"$REPO_DIR\" && git lfs install"
    else
        echo -e "${YELLOW}git-lfs not found; skipping 'git lfs install'.${NC}"
    fi

    # Install/refresh only pre-push hook (commit is free; checks happen on push)
    sudo -u "$NONROOT_USER" -H bash -lc "cd \"$REPO_DIR\" && pre-commit install -f --hook-type pre-push"

    # Ensure lint script is executable
    chmod +x scripts/lint.sh || true
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
    # install_qt6 TODO: Remove this once web UI is ready.
    install_opencv
    install_arm64_tools
    install_bazel
    install_git
    setup_user_permissions
    setup_ros2_environment
    install_linting_tools
    ensure_python_tooling
    install_precommit_and_hooks

    echo
    echo -e "${GREEN}========================================${NC}"
    echo -e "${GREEN}  Setup Complete!${NC}"
    echo -e "${GREEN}========================================${NC}"
    echo
    echo -e "${YELLOW}Important: You need to reboot for group changes to take effect${NC}"
}

# Execute main function
main "$@"
