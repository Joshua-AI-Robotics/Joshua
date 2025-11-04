# ROS2 Bazel Build Fix

## Summary

This document explains the fixes applied to enable building ROS2 nodes with Bazel using system-installed ROS2 libraries.

## Issues Fixed

### 1. Config Setting References ✅
**Problem**: Config settings used `//:cpu_x86_64` instead of `:cpu_x86_64`
**Fix**: Updated all config_setting references in `system_deps.bzl` to use local package references
**Files**: `system_deps.bzl`, `BUILD`

### 2. Inconsistent Dependencies ✅  
**Problem**: Mixed use of `@ros2_*` and `@local_deps` dependency references
**Fix**: Standardized all ROS2 dependencies to use `@local_deps`
**Files**: `ros2/BUILD`

### 3. GCC vs G++ Compiler ✅
**Problem**: Bazel was using `gcc` instead of `g++` for C++ compilation
**Fix**: Added compiler environment variables in `.bazelrc`:
```
build --repo_env=CC=g++
build --repo_env=CXX=g++
build --action_env=CC=g++
build --action_env=CXX=g++
```
**Files**: `.bazelrc`

### 4. Header Name Conflict ⚠️ REQUIRES MANUAL FIX
**Problem**: ROS2's `rmw/features.h` shadows the system `features.h`, causing compilation errors like:
```
error: missing binary operator before token "("
#if __GLIBC_USE (IEC_60559_BFP_EXT)
```

**Root Cause**: When Bazel adds `-isystem external/+_repo_rules+local_deps/opt/ros/humble/include/rmw`, the file `rmw/features.h` becomes accessible as `features.h`, shadowing `/usr/include/features.h`.

**Solution**: Rename the conflicting ROS2 header file:
```bash
./fix_ros2_headers.sh
```

Or manually:
```bash
sudo mv /opt/ros/humble/include/rmw/features.h /opt/ros/humble/include/rmw/rmw_features.h
```

## Files Modified

- `.bazelrc` - Added compiler configuration
- `BUILD` - Added platform config_settings  
- `system_deps.bzl` - Fixed config references, removed generic include paths
- `system_ros2_rules.bzl` - Added C++17 copts
- `ros2/BUILD` - Fixed dependency references

## Build Instructions

1. Apply the header fix:
   ```bash
   ./fix_ros2_headers.sh
   ```

2. Clean and rebuild:
   ```bash
   bazel clean
   bazel build //ros2:camera_publisher
   ```

## Technical Details

### Why the Header Conflict Occurs

Bazel's `cc_library` rule with `includes = ["path"]` generates `-isystem path` flags, which have higher priority than system headers. When we specify:

```python
includes = ["opt/ros/humble/include/rmw"]
```

Bazel adds: `-isystem external/+_repo_rules+local_deps/opt/ros/humble/include/rmw`

This makes `rmw/features.h` accessible as just `features.h`, which conflicts with the system header `/usr/include/features.h` that the C++ standard library expects.

### Why strip_include_prefix Didn't Work

Using `strip_include_prefix` alone doesn't add include paths to the compile command - it only reorganizes the header hierarchy. Without additional include paths, the headers aren't found at all.

### Alternative Solutions Considered

1. **Using `copts` with `-I` flags**: Rejected because absolute paths outside workspace are not allowed
2. **Completely rewriting header structure**: Too complex and fragile
3. **Creating wrapper headers**: Would require maintaining sync with ROS2 updates
4. **Renaming the file**: **Chosen solution** - simple and effective

## Cross-Compilation Support

The fixed `system_deps.bzl` supports cross-compilation for aarch64 (ARM64) targets like Jetson Orin Nano:

```bash
bazel build --config=jetson-orin-nano //ros2:camera_publisher
```

Platform-specific library paths are handled via `select()` statements in the library definitions.

## Future Improvements

- Consider using rules_ros2 for a more integrated approach
- Investigate patching ROS2's rmw package to rename features.h upstream
- Create a custom Bazel toolchain that handles ROS2 headers more elegantly

## Credits

Fixed by AI assistant on 2025-11-04

