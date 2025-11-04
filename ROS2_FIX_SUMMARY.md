# ROS2 Bazel Build Fix - Final Solution

## Problem Summary

The Bazel build for ROS2 targets (`//ros2:camera_publisher`, `//ros2:actuator_subscriber`, etc.) was failing with "rclcpp/rclcpp.hpp: No such file or directory" errors.

## Root Cause

ROS2 packages have a **nested directory structure**: `/opt/ros/humble/include/<package>/<package>/*.hpp` (note the double `<package>`).

When using a generic `strip_include_prefix = "opt/ros/humble/include"`, Bazel would:

1. Find header: `opt/ros/humble/include/rclcpp/rclcpp/rclcpp.hpp`
2. Strip prefix: `rclcpp/rclcpp/rclcpp.hpp`
3. Place in virtual includes: `_virtual_includes/rclcpp/rclcpp/rclcpp/rclcpp.hpp` (triple nesting!)
4. Code expects: `#include "rclcpp/rclcpp.hpp"` → looks for `_virtual_includes/rclcpp/rclcpp/rclcpp.hpp` ❌

Result: **File not found error**

## Solution

Use **package-specific** `strip_include_prefix` for each ROS2 library:

### Before (INCORRECT):
```python
cc_library(
    name = "rclcpp",
    hdrs = glob(["opt/ros/humble/include/rclcpp/**/*.hpp"]),
    strip_include_prefix = "opt/ros/humble/include",  # Too generic!
    ...
)
```

### After (CORRECT):
```python
cc_library(
    name = "rclcpp",
    hdrs = glob(["opt/ros/humble/include/rclcpp/**/*.hpp"]),
    strip_include_prefix = "opt/ros/humble/include/rclcpp",  # Package-specific!
    ...
)
```

With this fix:
1. Find header: `opt/ros/humble/include/rclcpp/rclcpp/rclcpp.hpp`
2. Strip prefix: `rclcpp/rclcpp.hpp`
3. Place in virtual includes: `_virtual_includes/rclcpp/rclcpp/rclcpp.hpp` (double nesting) ✅
4. Code includes: `#include "rclcpp/rclcpp.hpp"` → finds `_virtual_includes/rclcpp/rclcpp/rclcpp.hpp` ✅

## Additional Fixes

### 1. Header-Only Library Fix
Removed linkopts from `rosidl_runtime_cpp` library:
- It's a header-only library with no corresponding .so file
- Linking with `-lrosidl_runtime_cpp` was causing linker errors

```python
cc_library(
    name = "rosidl_runtime_cpp",
    hdrs = glob(["opt/ros/humble/include/rosidl_runtime_cpp/**/*.hpp"]),
    strip_include_prefix = "opt/ros/humble/include/rosidl_runtime_cpp",
    # No linkopts needed - header-only!
)
```

### 2. Config Settings
Ensured proper config_setting definitions exist in root BUILD file for platform selection:
```python
config_setting(
    name = "cpu_x86_64",
    constraint_values = ["@platforms//cpu:x86_64"],
    visibility = ["//visibility:public"],
)

config_setting(
    name = "cpu_aarch64",
    constraint_values = ["@platforms//cpu:aarch64"],
    visibility = ["//visibility:public"],
)
```

## Files Modified

1. **system_deps.bzl**:
   - Updated all ROS2 cc_library rules to use package-specific `strip_include_prefix`
   - Removed linkopts from `rosidl_runtime_cpp`
   - Pattern applied to all ROS2 libraries: rclcpp, rcl, rcutils, rmw, etc.

2. **BUILD** (root):
   - Added config_setting definitions with public visibility

3. **.bazelrc**:
   - Compiler settings to use g++ (previously configured)

## Verification

Successfully built multiple ROS2 targets:
- ✅ `//ros2:camera_publisher`
- ✅ `//ros2:actuator_subscriber`

## Key Takeaway

When wrapping system libraries with nested directory structures in Bazel, the `strip_include_prefix` must be specific enough to match the actual include patterns used in the source code. For ROS2's `/opt/ros/humble/include/<package>/<package>/` structure, use `strip_include_prefix = "opt/ros/humble/include/<package>"` for each library.

## Note

This solution **did not modify any system libraries** (as requested by the user). All changes were made only to Bazel build configuration files.

