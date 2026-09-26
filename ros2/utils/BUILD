# System ROS2 rules (using system-installed ROS2 libraries)
load("@rules_python//python:defs.bzl", "py_library")
load("//:system_ros2_rules.bzl", "ros2_cpp_binary")

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

cc_library(
    name = "qos_setting",
    hdrs = ["qos_setting.h"],
    deps = [
        "//ros2/proto:node_cc_proto",
        "@glog",
        "@ros2_deps//:rclcpp",
    ],
)

py_library(
    name = "ros_python_paths_py",
    srcs = ["ros_python_paths.py"],
)

py_library(
    name = "qos_setting_py",
    srcs = ["qos_setting.py"],
    deps = [
        "//ros2/proto:node_py_proto",
        # rclpy is provided by the selected Docker image's ROS2 installation.
    ],
)

cc_library(
    name = "packet_parser",
    srcs = ["packet_parser.cc"],
    hdrs = ["packet_parser.h"],
    deps = [
        "//robot/action/proto:action_packet_cc_proto",
        "//robot/perception/proto:perception_packet_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "packet_parser_test",
    srcs = ["packet_parser_test.cc"],
    deps = [
        ":packet_parser",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)
