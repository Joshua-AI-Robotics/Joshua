# Setup Scripts

This directory contains setup and installation scripts for the Joshua project, including comprehensive cross-compilation setup for NVIDIA Jetson Orin Nano (ARM64/AArch64).

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

## Cross-Compilation Setup for Jetson Orin Nano

This section describes the complete setup required for cross-compiling the Joshua project for NVIDIA Jetson Orin Nano (ARM64/AArch64).

### Prerequisites

- Ubuntu 22.04 (Jammy) host system
- Bazel build system
- GCC AArch64 cross-compiler

### Quick Start

#### 1. Install ARM64 Cross-Compilation Tools

```bash
sudo apt-get update
sudo apt-get install -y g++-aarch64-linux-gnu \
                        libstdc++-11-dev-arm64-cross \
                        libstdc++-12-dev-arm64-cross
```

#### 2. Install OpenCV ARM64 Libraries

Run the provided installation script:

```bash
sudo ./scripts/install_opencv_arm64.sh
```

This script will:
- Download OpenCV 4.5.4 ARM64 packages from Ubuntu ports
- Extract and install libraries to `/usr/lib/aarch64-linux-gnu/`
- Create necessary symlinks

#### 3. Build the Project

```bash
bazel build //node_generator:joshua_main --config=jetson-orin-nano
```

The compiled binary will be at: `bazel-bin/node_generator/joshua_main`

**First-time setup:**
Run this script once before building the project for ARM64:
```bash
# 1. Install cross-compilation tools
sudo apt-get install -y g++-aarch64-linux-gnu

# 2. Install OpenCV ARM64 libraries
sudo ./scripts/install_opencv_arm64.sh

# 3. Build the project
bazel build //node_generator:joshua_main --config=jetson-orin-nano
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

## Technical Details

### TLS Relocation Fix

When cross-compiling for AArch64, we encountered TLS (Thread-Local Storage) relocation errors:

```
ld.lld: error: relocation R_AARCH64_TLSLE_ADD_TPREL_HI12 against
__cxxabiv1::(anonymous namespace)::__globals()::eh_globals cannot be used with -shared
```

**Root Cause:**
The LLVM toolchain's `libc++abi.a` was compiled with the local-exec TLS model (`-ftls-model=local-exec`), which is incompatible with shared libraries. When the static archive was linked into ROS2 shared libraries, the linker rejected the TLSLE relocations.

**Solution:**
We modified the toolchain to compile `libc++abi` with:
- `-fPIC` (position-independent code)
- `-ftls-model=global-dynamic` (shared-library-compatible TLS model)

### Modified Files

The following files in the Bazel cache are modified to enable cross-compilation:

1. **`external/toolchains_llvm_bootstrapped+/third_party/llvm-project/20.1.5/libcxxabi/BUILD.tpl`**
   - Added `-fPIC` and `-ftls-model=global-dynamic` to `copts`

2. **`external/toolchains_llvm_bootstrapped+/toolchain/args/linux/BUILD.bazel`**
   - Restored original `-lc++` and `-lc++abi` linking (no changes needed)

### Bazel Configuration

The `.bazelrc` file contains the `orin-nano` configuration:

```bazel
build:orin-nano --platforms=//:orin_nano
build:orin-nano --cpu=arm64
build:orin-nano --experimental_cc_static_library
build:orin-nano --features=-static_link_cpp_runtimes
build:orin-nano --dynamic_mode=fully
build:orin-nano --copt='-fPIC'
build:orin-nano --copt='-ftls-model=global-dynamic'
build:orin-nano --@toolchains_llvm_bootstrapped//config:empty_sysroot=False
```

Key flags:
- `--features=-static_link_cpp_runtimes`: Prefer dynamic C++ runtime linking
- `--dynamic_mode=fully`: Enable full dynamic linking
- `--copt='-fPIC'`: Compile all code as position-independent
- `--copt='-ftls-model=global-dynamic'`: Use shared-library-compatible TLS model
- `--@toolchains_llvm_bootstrapped//config:empty_sysroot=False`: Allow access to system libraries

### System Dependencies

The `system_deps.bzl` file provides OpenCV library targets with AArch64-specific paths:

```python
linkopts = [
    "-L/usr/lib/aarch64-linux-gnu",
    "-Wl,-rpath,/usr/lib/aarch64-linux-gnu",
    "-lopencv_core",
]
```

## Troubleshooting

### Script-Specific Issues

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

### Cross-Compilation Issues

**TLS Relocation Errors Return After Clean Build**

If you run `bazel clean --expunge`, the toolchain cache will be regenerated and the `libc++abi` modifications will be lost.

**Solution:**
After a full clean, re-apply the modifications:

1. Build once to generate the toolchain cache
2. Locate the cache: `~/.cache/bazel/_bazel_$USER/*/external/toolchains_llvm_bootstrapped+/`
3. Edit `third_party/llvm-project/20.1.5/libcxxabi/BUILD.tpl`
4. Add `-fPIC` and `-ftls-model=global-dynamic` to the `copts` array (around line 54-64)

**OpenCV Libraries Not Found**

If you see errors like:
```
ld.lld: error: unable to find library -lopencv_core
```

Re-run the installation script:
```bash
sudo ./scripts/install_opencv_arm64.sh
```

**Undefined Symbols from libstdc++**

If you see undefined symbols with `std::__1::` namespace (libc++) but the linker is trying to use libstdc++:

**Cause:** ABI mismatch between libc++ (used by code) and libstdc++ (used by linker)

**Solution:** Ensure the toolchain is using libc++/libc++abi, not libstdc++. The current configuration should handle this correctly.

## Deployment to Jetson Orin Nano

### 1. Package the Binary

```bash
bazel build //node_generator:joshua_main_pkg --config=jetson-orin-nano
```

This creates a tar.gz archive with all dependencies.

### 2. Transfer to Device

```bash
scp bazel-bin/node_generator/joshua_main_pkg.tar.gz user@jetson-orin:/path/to/deploy/
```

### 3. Extract and Run on Device

```bash
ssh user@jetson-orin
cd /path/to/deploy/
tar -xzf joshua_main_pkg.tar.gz
cd joshua_main
./joshua_main
```

## Additional Resources

- [Bazel C++ Toolchain Configuration](https://bazel.build/docs/cc-toolchain-config-reference)
- [LLVM Toolchain for Bazel](https://github.com/bazel-contrib/toolchains_llvm)
- [TLS Models in GCC/Clang](https://maskray.me/blog/2021-02-14-all-about-thread-local-storage)
- [Position Independent Code (PIC)](https://en.wikipedia.org/wiki/Position-independent_code)

## Contributing

If you make improvements to the cross-compilation setup, please update this document and submit a pull request.
