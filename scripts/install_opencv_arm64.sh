#!/bin/bash
# Install OpenCV ARM64 libraries for cross-compilation to ARM64 Linux platforms
# This script downloads and installs OpenCV 4.5.4 ARM64 libraries to /usr/lib/aarch64-linux-gnu/

set -e  # Exit on error

OPENCV_VERSION="4.5.4+dfsg-9ubuntu4"
TEMP_DIR="/tmp/opencv-arm64-install"
INSTALL_DIR="/usr/lib/aarch64-linux-gnu"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Installing OpenCV ARM64 libraries for ARM64 cross-compilation${NC}"
echo "Version: ${OPENCV_VERSION}"
echo

# Check if running with sudo
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}Error: This script must be run with sudo${NC}"
    echo "Usage: sudo ./scripts/install_opencv_arm64.sh"
    exit 1
fi

# Create temporary directory
echo -e "${YELLOW}Creating temporary directory...${NC}"
mkdir -p "$TEMP_DIR"
cd "$TEMP_DIR"

# List of OpenCV packages to download
PACKAGES=(
    "libopencv-core4.5d_${OPENCV_VERSION}_arm64.deb"
    "libopencv-imgproc4.5d_${OPENCV_VERSION}_arm64.deb"
    "libopencv-imgcodecs4.5d_${OPENCV_VERSION}_arm64.deb"
    "libopencv-highgui4.5d_${OPENCV_VERSION}_arm64.deb"
    "libopencv-videoio4.5d_${OPENCV_VERSION}_arm64.deb"
)

# Download packages
echo -e "${YELLOW}Downloading OpenCV ARM64 packages from Ubuntu ports...${NC}"
for package in "${PACKAGES[@]}"; do
    if [ ! -f "$package" ]; then
        echo "  Downloading $package..."
        wget -q "http://ports.ubuntu.com/pool/universe/o/opencv/$package" || {
            echo -e "${RED}Failed to download $package${NC}"
            exit 1
        }
    else
        echo "  $package already downloaded"
    fi
done

# Extract packages
echo -e "${YELLOW}Extracting packages...${NC}"
for package in "${PACKAGES[@]}"; do
    dpkg-deb -x "$package" extracted/
done

# Create installation directory if it doesn't exist
echo -e "${YELLOW}Creating installation directory...${NC}"
mkdir -p "$INSTALL_DIR"

# Copy libraries
echo -e "${YELLOW}Installing libraries to $INSTALL_DIR...${NC}"
if [ -d "extracted/usr/lib/aarch64-linux-gnu" ]; then
    cp -v extracted/usr/lib/aarch64-linux-gnu/libopencv*.so* "$INSTALL_DIR/"
else
    echo -e "${RED}Error: Extracted libraries not found${NC}"
    exit 1
fi

# Create symlinks
echo -e "${YELLOW}Creating symlinks...${NC}"
cd "$INSTALL_DIR"
for lib in libopencv_{core,imgproc,imgcodecs,highgui,videoio}; do
    if [ -f "${lib}.so.4.5d" ]; then
        ln -sf "${lib}.so.4.5d" "${lib}.so"
        echo "  Created symlink: ${lib}.so -> ${lib}.so.4.5d"
    fi
done

# Verify installation
echo
echo -e "${YELLOW}Verifying installation...${NC}"
ls -lh "$INSTALL_DIR"/libopencv*.so | head -10

# Cleanup
echo
echo -e "${YELLOW}Cleaning up temporary files...${NC}"
rm -rf "$TEMP_DIR"

echo
echo -e "${GREEN}✓ OpenCV ARM64 libraries installed successfully!${NC}"
echo
echo "Installed libraries:"
echo "  - libopencv_core.so"
echo "  - libopencv_imgproc.so"
echo "  - libopencv_imgcodecs.so"
echo "  - libopencv_highgui.so"
echo "  - libopencv_videoio.so"
echo
echo "You can now build the project for ARM64 platforms with:"
echo "  bazel build //node_generator:joshua_main --config=<your-arm64-config>"
echo "  # Examples:"
echo "  #   --config=jetson-orin-nano    (for Jetson Orin Nano)"
echo "  #   --config=raspberry-pi        (for Raspberry Pi)"
echo "  #   --config=jetson-xavier        (for Jetson Xavier)"

