load("@com_github_mvukov_rules_ros2//ros2:py_defs.bzl", "ros2_py_binary")
load("@com_github_mvukov_rules_ros2//ros2:cc_defs.bzl", "ros2_cpp_binary")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("//ros2:ros2_wrapper.bzl", "ros2_wrapper_script", "ros2_cpp_binary_with_wrapper", "ros2_py_binary_with_wrapper")
load("@rules_python//python:defs.bzl", "py_library")

package(default_visibility = ["//visibility:public"])

# ==============================================================================
# ROS2 Binary Wrapper System (C++ and Python)
# ==============================================================================
# 
# This BUILD file uses custom macros that automatically generate both the ROS2
# binary and a standalone wrapper script for execution outside of `bazel run`.
#
# - C++:   ros2_cpp_binary_with_wrapper(name = ...)
# - Python:ros2_py_binary_with_wrapper(name = ...)
#
# PURPOSE: Enable running ROS2 nodes standalone without Bazel commands
# - Wrapper scripts allow deployment/execution without Bazel installation
# - Binaries can run independently on target systems
# - No need for `bazel run` in production environments
#
# How it works:
# 1. The macros create the normal ROS2 targets with set_up_ament=True
# 2. They also generate a wrapper script (e.g., <name>_wrapper.sh)
# 3. The wrapper script is added as a data dependency so it builds automatically
#
# Benefits:
# - Single build command: `bazel build ros2:<target>` builds both binary and wrapper
# - Standalone execution without Bazel dependency
# - Node generator can uniformly launch both C++ and Python nodes via wrappers
#
# Usage examples:
# - Build: `bazel build ros2:mock_inference_py`
# - Run standalone: `cd bazel-bin && ./ros2/mock_inference_py_wrapper.sh <node_name> <node_id> <config_path>`
# - Traditional still works: `bazel run ros2:mock_inference_py -- <args>`
#
# The wrapper scripts:
# - Set up AMENT_PREFIX_PATH
# - Change to the correct working directory for runfiles (when needed)
# - Execute the underlying binary/launcher with all libraries resolved
# - Work from the bazel-bin directory without needing `bazel run`
# - Suitable for use by the node generator when spawning processes
#
# ==============================================================================

cc_library(
    name = "node_runner",
    hdrs = ["node_runner.h"],
    data = [
        "//config/config_preset:config_presets",
    ],
    deps = [
        "@ros2_rclcpp//:rclcpp",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "//config:config_utils",
        "//config/proto:config_cc_proto",
    ],
)

py_library(
    name = "node_runner_py",
    srcs = [
        "node_runner.py",
    ],
    deps = [
        "@ros2_rclpy//:rclpy",
        "//config/proto:config_py_proto",
        requirement("protobuf"),
    ],
    visibility = ["//visibility:public"],
)

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
        "//robot/perception/factory:perception_factory",
        "//config/proto:config_cc_proto",
        "//robot/perception/proto:perception_packet_cc_proto",
        "//ros2:node_runner",
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
        "//robot/perception/proto:perception_packet_cc_proto",
        "//robot/perception/proto:perception_cc_proto",
        "//ros2:node_runner",
    ],
)

ros2_cpp_binary_with_wrapper(
    name = "actuator_subscriber",
    srcs = ["actuator_subscriber.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    deps = [
        "@ros2_rclcpp//:rclcpp",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_common_interfaces//:cpp_std_srvs",
        "//config/proto:config_cc_proto",
        "//robot/action/factory:action_factory",
        "//robot/action/proto:action_packet_cc_proto",
        "//robot/action/proto:action_cc_proto",
        "//ros2:node_runner",
    ],
)

ros2_cpp_binary_with_wrapper(
    name = "mock_inference",
    srcs = ["mock_inference.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    deps = [
        "@ros2_rclcpp//:rclcpp",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_common_interfaces//:cpp_sensor_msgs",
        "//config/proto:config_cc_proto",
        "//config/proto:robot_cc_proto",
        "//config/proto:ai_cc_proto",
        "//ros2:node_runner",
    ],
)

ros2_py_binary_with_wrapper(
    name = "mock_inference_py",
    srcs = [
        "mock_inference.py",
    ],
    main = "mock_inference.py",
    deps = [
        "@ros2_rclpy//:rclpy",
        "@ros2_common_interfaces//:py_std_msgs",
        "@ros2_common_interfaces//:py_sensor_msgs",
        "//config/proto:config_py_proto",
        "//ros2:node_runner_py",
    ],
    data = [
        "//config/config_preset:config_presets",
    ],
)

ros2_cpp_binary_with_wrapper(
    name = "operational_limit_calibration",
    srcs = ["operational_limit_calibration.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    deps = [
        "@ros2_rclcpp//:rclcpp",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "//config/proto:config_cc_proto",
        "//ros2:node_runner",
    ],
)

# Tarball of all ROS2 executables with wrappers and their runfiles
pkg_tar(
    name = "ros2_executables_tar",
    srcs = [
        ":camera_publisher",
        ":camera_publisher_wrapper",
        ":camera_publisher_impl",
        ":encoder_publisher",
        ":encoder_publisher_wrapper",
        ":encoder_publisher_impl",
        ":actuator_subscriber",
        ":actuator_subscriber_wrapper",
        ":actuator_subscriber_impl",
        ":mock_inference",
        ":mock_inference_wrapper",
        ":mock_inference_impl",
        ":mock_inference_py",
        ":mock_inference_py_wrapper",
        ":mock_inference_py_impl",
        ":operational_limit_calibration",
        ":operational_limit_calibration_wrapper",
        ":operational_limit_calibration_impl",
        "//config/config_preset:config_presets", # TODO: Remove this and move this to node_generator.
    ],
    include_runfiles = True,
    strip_prefix = "ros2",
    extension = ".gz",
    mode = "0755",
    visibility = ["//visibility:public"],
)
