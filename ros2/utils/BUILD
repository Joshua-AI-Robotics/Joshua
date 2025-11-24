# System ROS2 rules (using system-installed ROS2 libraries)
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")
load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_library")
load("//:system_ros2_rules.bzl", "ros2_cpp_binary", "ros2_py_binary")

package(default_visibility = ["//visibility:public"])

ros2_cpp_binary(
    name = "xbox_controller_publisher",
    srcs = ["xbox_controller_publisher.cc"],
    deps = [
        "//utils:xbox_controller",
        "@ros2_deps//:cpp_std_msgs",
        "@ros2_deps//:rclcpp",
    ],
)

ros2_cpp_binary(
    name = "camera_subscriber",
    srcs = ["camera_subscriber.cc"],
    deps = [
        "@local_deps//:opencv",
        "@ros2_deps//:cpp_sensor_msgs",
        "@ros2_deps//:rclcpp",
    ],
)

ros2_cpp_binary(
    name = "keyboard_publisher",
    srcs = ["keyboard_publisher.cc"],
    deps = [
        "@ros2_deps//:cpp_std_msgs",
        "@ros2_deps//:rclcpp",
    ],
)

ros2_py_binary(
    name = "pointcloud2_visualizer",
    srcs = ["pointcloud2_visualizer.py"],
    main = "pointcloud2_visualizer.py",
    deps = [
        # Note: Python ROS2 messages and rclpy are available via system Python installation
        # Ensure ROS2 is sourced: source /opt/ros/humble/setup.bash
        requirement("opencv-python"),
        requirement("numpy"),
    ],
    tags = ["manual"],
)

cc_library(
    name = "qos_setting",
    hdrs = ["qos_setting.h"],
    deps = [
        "//ros2/proto:node_cc_proto",
        "@ros2_deps//:rclcpp",
    ],
)

py_library(
    name = "qos_setting_py",
    srcs = ["qos_setting.py"],
    deps = [
        "//ros2/proto:node_py_proto",
        # rclpy is available via system Python installation
        # Ensure ROS2 is sourced: source /opt/ros/humble/setup.bash
    ],
)
