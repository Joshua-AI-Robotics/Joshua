#!/bin/bash
# Joshua Project Setup Script
# Combined script for both Development and Runtime environments

set -e  # Exit on error

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default environment
ENV_MODE=""

# Parse arguments
for arg in "$@"; do
    case $arg in
        --env=*)
            ENV_MODE="${arg#*=}"
            shift
            ;;
        *)
            # unknown option
            ;;
    esac
done

if [ -z "$ENV_MODE" ]; then
    echo -e "${RED}Error: Environment not specified.${NC}"
    echo -e "Usage: sudo ./scripts/setup.sh --env=dev OR --env=runtime"
    exit 1
fi

if [[ "$ENV_MODE" != "dev" && "$ENV_MODE" != "runtime" ]]; then
    echo -e "${RED}Error: Invalid environment '$ENV_MODE'. Must be 'dev' or 'runtime'.${NC}"
    exit 1
fi

echo -e "${GREEN}=== Joshua Project Setup: $ENV_MODE ===${NC}"

# Check if running with sudo
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run with sudo${NC}"
    echo "Usage: sudo ./scripts/setup.sh --env=$ENV_MODE"
    exit 1
fi

# Check repo root (only for dev, or if git is present)
if [ "$ENV_MODE" == "dev" ]; then
    if ! command -v git &> /dev/null; then
        echo -e "${YELLOW}Git not found. Installing git to verify repo root...${NC}"
        sudo apt-get update && sudo apt-get install -y git
    fi
    if [ "$(pwd)" != "$(git rev-parse --show-toplevel)" ]; then
        echo -e "${RED}Error: This script must be run from the root of the repository${NC}"
        echo "Usage: cd $(git rev-parse --show-toplevel) && sudo ./scripts/setup.sh --env=$ENV_MODE"
        exit 1
    fi
else
    # Runtime mode: Just check if requirements.txt exists in current dir if we are going to use it
    if [ ! -f "requirements.txt" ]; then
         echo -e "${YELLOW}Warning: requirements.txt not found in current directory.$(pwd)${NC}"
    fi
fi

# Determine non-root user
NONROOT_USER=${SUDO_USER:-$USER}
if [ -z "$NONROOT_USER" ] || [ "$NONROOT_USER" = "root" ]; then
    echo -e "${YELLOW}Warning: Could not determine non-root user. Some user-specific steps might be skipped or run as root.${NC}"
fi

# --- Common Functions ---

check_ubuntu_version() {
    echo -e "${BLUE}Checking Ubuntu version...${NC}"
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        UBUNTU_CODENAME=$UBUNTU_CODENAME
        VERSION_ID=$VERSION_ID
    else
        echo -e "${RED}Error: Cannot detect OS version.${NC}"
        exit 1
    fi

    if [ "$VERSION_ID" == "22.04" ]; then
        echo -e "${GREEN}Ubuntu 22.04 (Humble) detected${NC}"
        ROS_DISTRO="humble"
    elif [ "$VERSION_ID" == "24.04" ]; then
        echo -e "${GREEN}Ubuntu 24.04 (Jazzy) detected${NC}"
        ROS_DISTRO="jazzy"
    else
        echo -e "${YELLOW}Warning: Non-standard Ubuntu version ($VERSION_ID). Defaulting to Humble logic, but things might break.${NC}"
        ROS_DISTRO="humble"
    fi
}

update_packages() {
    echo -e "${BLUE}Updating package lists...${NC}"
    sudo apt-get update
    sudo apt-get install -y software-properties-common curl locales gnupg
    
    # Ensure locale
    sudo locale-gen en_US en_US.UTF-8
    sudo update-locale LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8
    export LANG=en_US.UTF-8
}

install_ros2_repos() {
    echo -e "${BLUE}Setting up ROS 2 repositories...${NC}"
    if [ ! -f /etc/apt/sources.list.d/ros2.list ]; then
        sudo add-apt-repository universe -y
        sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -o /usr/share/keyrings/ros-archive-keyring.gpg
        echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu $UBUNTU_CODENAME main" | sudo tee /etc/apt/sources.list.d/ros2.list > /dev/null
        sudo apt-get update
    fi
}

install_ros2_packages() {
    echo -e "${BLUE}Installing ROS 2 ($ROS_DISTRO)...${NC}"
    if [ "$ENV_MODE" == "dev" ]; then
        # Dev: Full Desktop install
        sudo apt-get install -y ros-$ROS_DISTRO-desktop
        sudo apt-get install -y python3-rosdep python3-rosinstall python3-rosinstall-generator python3-wstool build-essential python3-colcon-common-extensions
        
        echo -e "${BLUE}Initializing rosdep...${NC}"
        sudo rosdep init || true
        echo -e "${BLUE}Updating rosdep for user $NONROOT_USER...${NC}"
        sudo -u "$NONROOT_USER" rosdep update
    else
        # Runtime: Base + Essential Interfaces
        sudo apt-get install -y ros-$ROS_DISTRO-ros-base
        sudo apt-get install -y \
            ros-$ROS_DISTRO-std-msgs \
            ros-$ROS_DISTRO-sensor-msgs \
            ros-$ROS_DISTRO-geometry-msgs \
            ros-$ROS_DISTRO-std-srvs \
            ros-$ROS_DISTRO-nav-msgs

        if [ "$ROS_DISTRO" == "jazzy" ]; then
             sudo apt-get install -y \
                ros-$ROS_DISTRO-service-msgs \
                ros-$ROS_DISTRO-type-description-interfaces \
                ros-$ROS_DISTRO-rosidl-dynamic-typesupport
        fi
    fi
}

install_opencv() {
    echo -e "${BLUE}Installing OpenCV...${NC}"
    sudo apt-get install -y libopencv-dev
}

setup_user_permissions() {
    echo -e "${BLUE}Setting up user permissions...${NC}"
    if [ -n "$NONROOT_USER" ]; then
        sudo usermod -aG dialout $NONROOT_USER
        sudo usermod -aG video $NONROOT_USER
        
        if [ "$ENV_MODE" == "dev" ]; then
            sudo usermod -aG docker $NONROOT_USER
        fi
    fi
}

setup_ros2_environment() {
    echo -e "${BLUE}Setting up ROS 2 environment in .bashrc...${NC}"
    if [ -n "$NONROOT_USER" ]; then
        USER_HOME=$(getent passwd "$NONROOT_USER" | cut -d: -f6)
    else
        USER_HOME=$HOME
    fi

    SETUP_CMD="source /opt/ros/$ROS_DISTRO/setup.bash"
    
    if ! grep -q "$SETUP_CMD" "$USER_HOME/.bashrc"; then
        echo "" >> "$USER_HOME/.bashrc"
        echo "# ROS 2 $ROS_DISTRO Setup" >> "$USER_HOME/.bashrc"
        echo "$SETUP_CMD" >> "$USER_HOME/.bashrc"
        echo -e "${GREEN}Added ROS 2 setup to $USER_HOME/.bashrc${NC}"
    else
        echo -e "${GREEN}ROS 2 setup already in .bashrc${NC}"
    fi
}

install_python_deps() {
    echo -e "${BLUE}Installing Python dependencies...${NC}"
    sudo apt-get install -y python3-pip python3-venv

    if [ -f "requirements.txt" ]; then
        echo -e "${BLUE}Installing dependencies from requirements.txt...${NC}"
        # Install to user site-packages
        if [ -n "$NONROOT_USER" ]; then
             sudo -u "$NONROOT_USER" -H bash -lc 'python3 -m pip install --user --upgrade -r requirements.txt --break-system-packages || python3 -m pip install --user --upgrade -r requirements.txt'
        else
             # Fallback for root (runtime usually) - try --break-system-packages
             pip3 install --upgrade -r requirements.txt --break-system-packages || pip3 install --upgrade -r requirements.txt
        fi
    else
        echo -e "${YELLOW}requirements.txt not found. Skipping.${NC}"
    fi
}

install_cudnn() {
    echo -e "${BLUE}Checking CuDNN version...${NC}"
    local CUDNN_MAJOR=$(grep '#define CUDNN_MAJOR' /usr/include/cudnn_version.h 2>/dev/null | awk '{print $3}')
    local CUDNN_MINOR=$(grep '#define CUDNN_MINOR' /usr/include/cudnn_version.h 2>/dev/null | awk '{print $3}')

    if [ -n "$CUDNN_MAJOR" ] && [ "$CUDNN_MAJOR" -ge 9 ] && [ "$CUDNN_MINOR" -ge 1 ]; then
        echo -e "${GREEN}CuDNN ${CUDNN_MAJOR}.${CUDNN_MINOR} already installed (>= 9.1)${NC}"
        return 0
    fi

    echo -e "${YELLOW}CuDNN ${CUDNN_MAJOR:-?}.${CUDNN_MINOR:-?} is too old for JAX/MJX (need >= 9.1). Upgrading...${NC}"

    if [ ! -f /usr/share/keyrings/cuda-archive-keyring.gpg ] && [ ! -f /etc/apt/sources.list.d/cuda-*.list ]; then
        local KEYRING_URL="https://developer.download.nvidia.com/compute/cuda/repos/ubuntu$(lsb_release -rs | tr -d '.')/$(dpkg --print-architecture)/cuda-keyring_1.1-1_all.deb"
        wget -q "$KEYRING_URL" -O /tmp/cuda-keyring.deb
        dpkg -i /tmp/cuda-keyring.deb
        rm -f /tmp/cuda-keyring.deb
    fi

    apt-get update
    apt-get install -y libcudnn9-cuda-12

    local NEW_MINOR=$(grep '#define CUDNN_MINOR' /usr/include/cudnn_version.h 2>/dev/null | awk '{print $3}')
    echo -e "${GREEN}CuDNN upgraded to 9.${NEW_MINOR:-?}${NC}"
}

fix_nvidia_libs() {
    echo -e "${BLUE}Checking for NVIDIA libraries...${NC}"
    
    # 1. Fix for libcusparseLt.so.0 (Required by Torch)
    local LIB_NAME="libcusparseLt.so.0"
    local LIB_PATH=""

    # Check user then system site-packages
    if [ -n "$NONROOT_USER" ]; then
        local USER_SITE=$(sudo -u "$NONROOT_USER" python3 -m site --user-site)
        [ -d "$USER_SITE" ] && LIB_PATH=$(find "$USER_SITE" -name "$LIB_NAME" -print -quit)
    fi
    [ -z "$LIB_PATH" ] && LIB_PATH=$(find /usr/local/lib/python* /usr/lib/python* -name "$LIB_NAME" -print -quit 2>/dev/null)

    if [ -n "$LIB_PATH" ]; then
        if [ ! -e "/usr/lib/$LIB_NAME" ]; then
             echo "Symlinking $LIB_NAME to /usr/lib"
             sudo ln -sf "$LIB_PATH" "/usr/lib/$LIB_NAME"
             sudo ldconfig
        fi
    else
        echo -e "${YELLOW}Warning: $LIB_NAME not found.${NC}"
    fi

    # 2. Fix for libnccl.so.2 (Required by Torch, prefer pip version over system)
    local NCCL_NAME="libnccl.so.2"
    local NCCL_PATH=""
    
    if [ -n "$NONROOT_USER" ]; then
        local USER_SITE=$(sudo -u "$NONROOT_USER" python3 -m site --user-site)
        [ -d "$USER_SITE" ] && NCCL_PATH=$(find "$USER_SITE" -name "$NCCL_NAME" -print -quit)
    fi
    [ -z "$NCCL_PATH" ] && NCCL_PATH=$(find /usr/local/lib/python* /usr/lib/python* -name "$NCCL_NAME" -print -quit 2>/dev/null)

    if [ -n "$NCCL_PATH" ]; then
         # Ensure pip version takes precedence by linking to /usr/local/lib
         if [ ! -L "/usr/local/lib/$NCCL_NAME" ]; then
             echo "Symlinking pip $NCCL_NAME to /usr/local/lib to take precedence"
             sudo ln -sf "$NCCL_PATH" "/usr/local/lib/$NCCL_NAME"
             sudo ldconfig
         fi
    fi
    
    # 3. Fix for Python Package Conflicts (SymPy, SciPy)
    # Hide system packages that conflict with pip versions but cannot be removed due to ROS deps.
    if [ -d "/usr/lib/python3/dist-packages/sympy" ]; then 
        echo "Hiding system sympy to resolve version conflict"
        sudo mv /usr/lib/python3/dist-packages/sympy /usr/lib/python3/dist-packages/sympy.bak
    fi
    
    if [ -d "/usr/lib/python3/dist-packages/scipy" ]; then
         echo "Hiding system scipy to resolve version conflict"
         sudo mv /usr/lib/python3/dist-packages/scipy /usr/lib/python3/dist-packages/scipy.bak
    fi
}

# --- Dev Only Functions ---

install_docker() {
    echo -e "${BLUE}Checking for Docker...${NC}"
    if ! command -v docker &> /dev/null; then
        echo -e "${BLUE}Installing Docker...${NC}"
        sudo install -m 0755 -d /etc/apt/keyrings
        curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
        sudo chmod a+r /etc/apt/keyrings/docker.gpg
        echo \
          "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/ubuntu \
          $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | \
          sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
        sudo apt-get update
        sudo apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
    else
        echo -e "${GREEN}Docker already installed${NC}"
    fi
}

install_bazel() {
    echo -e "${BLUE}Checking for Bazel...${NC}"
    if command -v bazel &> /dev/null; then
        echo -e "${GREEN}Bazel already installed${NC}"
        return 0
    fi
    
    ARCH=$(uname -m)
    case "$ARCH" in
        x86_64) BAZEL_ARCH="amd64" ;;
        aarch64|arm64) BAZEL_ARCH="arm64" ;;
        *) echo -e "${RED}Unsupported architecture: $ARCH${NC}"; return 1 ;;
    esac

    echo -e "${BLUE}Installing Bazelisk for linux-$BAZEL_ARCH...${NC}"
    curl -L "https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-$BAZEL_ARCH" | sudo tee /usr/local/bin/bazel > /dev/null
    sudo chmod +x /usr/local/bin/bazel
}

install_dev_tools() {
    echo -e "${BLUE}Installing dev tools (git, linters, arm64-tools)...${NC}"
    sudo apt-get install -y git git-lfs clang-format g++-aarch64-linux-gnu
    
    # Pre-commit hooks (only if git repo is present)
    if [ -n "$NONROOT_USER" ] && [ -d ".git" ]; then
        sudo -u "$NONROOT_USER" -H bash -lc 'python3 -m pip install --user --upgrade pre-commit black flake8 isort'
        
        # Git LFS
        if command -v git-lfs >/dev/null; then
            sudo -u "$NONROOT_USER" -H bash -lc "git lfs install"
        fi
        
        # Pre-push hook
        sudo -u "$NONROOT_USER" -H bash -lc "pre-commit install -f --hook-type pre-push"
    fi
    
    # Buildifier
    ARCH=$(uname -m)
    if [ "$ARCH" == "x86_64" ]; then
        URL="https://github.com/bazelbuild/buildtools/releases/download/v6.4.0/buildifier-linux-amd64"
    elif [[ "$ARCH" == "aarch64" || "$ARCH" == "arm64" ]]; then
        URL="https://github.com/bazelbuild/buildtools/releases/download/v6.4.0/buildifier-linux-arm64"
    fi
    
    if [ -n "$URL" ]; then
        curl -L "$URL" | sudo tee /usr/local/bin/buildifier >/dev/null
        sudo chmod 0755 /usr/local/bin/buildifier
    fi
}

# --- Main Execution Flow ---

check_ubuntu_version
update_packages
install_ros2_repos
install_ros2_packages
install_opencv
install_python_deps
install_cudnn
fix_nvidia_libs
setup_user_permissions
setup_ros2_environment

if [ "$ENV_MODE" == "dev" ]; then
    install_docker
    install_bazel
    install_dev_tools
fi

echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  $ENV_MODE Setup Complete!${NC}"
echo -e "${GREEN}========================================${NC}"
if [ -n "$NONROOT_USER" ]; then
    echo -e "${YELLOW}Important: You need to reboot for group changes to take effect${NC}"
fi

