load("@com_github_mvukov_rules_ros2//ros2:py_defs.bzl", "ros2_py_binary")
load("@com_github_mvukov_rules_ros2//ros2:cc_defs.bzl", "ros2_cpp_binary")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("//ros2:ros2_wrapper.bzl", "ros2_wrapper_script", "ros2_cpp_binary_with_wrapper")

package(default_visibility = ["//visibility:public"])

# ==============================================================================
# ROS2 Binary Wrapper System
# ==============================================================================
# 
# This BUILD file uses a custom macro `ros2_cpp_binary_with_wrapper` that 
# automatically generates both the ROS2 binary and a standalone wrapper script.
#
# PURPOSE: Enable running ROS2 nodes standalone without Bazel commands
# - Wrapper scripts allow deployment/execution without Bazel installation
# - Binaries can run independently on target systems
# - No need for `bazel run` in production environments
#
# How it works:
# 1. ros2_cpp_binary_with_wrapper() creates the normal ROS2 binary with set_up_ament=True
# 2. It also generates a wrapper script (e.g., action_subscriber_wrapper.sh)
# 3. The wrapper script is added as a data dependency to ensure it builds automatically
#
# Benefits:
# - Single build command: `bazel build ros2:action_subscriber` 
# - Generates both binary and wrapper script in one go
# - No need to remember to build wrapper scripts separately
# - Standalone execution without Bazel dependency
#
# Usage:
# 1. Build: `bazel build ros2:action_subscriber`
# 2. Run standalone: `cd bazel-bin && ./ros2/action_subscriber_wrapper.sh [args]`
# 3. Or use traditional: `bazel run ros2:action_subscriber [args]` (still works!)
#
# The wrapper scripts:
# - Set up proper environment variables (AMENT_PREFIX_PATH)
# - Change to the correct working directory for runfiles
# - Execute the binary with all shared libraries found correctly
# - Work from the bazel-bin directory without needing `bazel run`
# - Enable deployment to systems without Bazel installed
#
# ==============================================================================

ros2_cpp_binary_with_wrapper(
    name = "camera_publisher",
    srcs = ["camera_publisher.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    deps = [
        "@ros2_rclcpp//:rclcpp",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_common_interfaces//:cpp_sensor_msgs",
        "@local_deps//:opencv",
        "//robot/perception/factory:perception_factory",
        "//config/proto:config_cc_proto",
        "//config:config_utils",
    ],
)

ros2_cpp_binary_with_wrapper(
    name = "encoder_publisher",
    srcs = ["encoder_publisher.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    deps = [
        "@ros2_rclcpp//:rclcpp",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "//robot/perception/factory:perception_factory",
        "//config/proto:config_cc_proto",
        "//config:config_utils",
    ],
)

ros2_cpp_binary_with_wrapper(
    name = "action_subscriber",
    srcs = ["action_subscriber.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    deps = [
        "@ros2_rclcpp//:rclcpp",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_common_interfaces//:cpp_std_srvs",
        "//config/proto:config_cc_proto",
        "//config:config_utils",
        "//robot/action/factory:action_factory",
    ],
)
