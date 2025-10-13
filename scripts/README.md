# Setup Scripts

This directory contains setup and installation scripts for the Joshua project.

## Available Scripts

### `install_opencv_arm64.sh`

Installs OpenCV 4.5.4 ARM64 libraries required for cross-compiling to Jetson Orin Nano.

**Usage:**
```bash
sudo ./scripts/install_opencv_arm64.sh
```

**What it does:**
- Downloads OpenCV ARM64 packages (core, imgproc, imgcodecs, highgui, videoio) from Ubuntu ports
- Extracts and installs libraries to `/usr/lib/aarch64-linux-gnu/`
- Creates necessary `.so` symlinks for the linker
- Cleans up temporary files

**Requirements:**
- Must be run with sudo (requires root permissions to install to `/usr/lib`)
- Internet connection to download packages (~2.2 MB)
- Ubuntu 22.04 (Jammy) host system

**First-time setup:**
Run this script once before building the project for ARM64:
```bash
# 1. Install cross-compilation tools
sudo apt-get install -y g++-aarch64-linux-gnu

# 2. Install OpenCV ARM64 libraries
sudo ./scripts/install_opencv_arm64.sh

# 3. Build the project
bazel build //node_generator:joshua_main --config=orin-nano
```

**Verification:**
After running, verify installation:
```bash
ls -lh /usr/lib/aarch64-linux-gnu/libopencv*.so
```

You should see:
```
libopencv_core.so -> libopencv_core.so.4.5d
libopencv_highgui.so -> libopencv_highgui.so.4.5d
libopencv_imgcodecs.so -> libopencv_imgcodecs.so.4.5d
libopencv_imgproc.so -> libopencv_imgproc.so.4.5d
libopencv_videoio.so -> libopencv_videoio.so.4.5d
```

## Troubleshooting

**"Error: This script must be run with sudo"**
- The script needs root permissions to install libraries to `/usr/lib`
- Run with: `sudo ./scripts/install_opencv_arm64.sh`

**"Failed to download ..."**
- Check your internet connection
- Verify you can access `ports.ubuntu.com`
- Try manually downloading one package to test:
  ```bash
  wget http://ports.ubuntu.com/pool/universe/o/opencv/libopencv-core4.5d_4.5.4+dfsg-9ubuntu4_arm64.deb
  ```

**Libraries already installed**
- The script is idempotent - safe to run multiple times
- It will skip already downloaded packages in `/tmp`

## Additional Documentation

For detailed information about cross-compilation setup, see:
- [Cross-Compilation Setup Guide](../docs/CROSS_COMPILE_SETUP.md)

