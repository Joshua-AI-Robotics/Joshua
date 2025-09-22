load("@com_github_mvukov_rules_ros2//ros2:py_defs.bzl", "ros2_py_binary")
load("@com_github_mvukov_rules_ros2//ros2:cc_defs.bzl", "ros2_cpp_binary")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")
load("@rules_python//python:defs.bzl", "py_library")

package(default_visibility = ["//visibility:public"])

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

ros2_cpp_binary(
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
    set_up_ament = True,
)

ros2_cpp_binary(
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
    set_up_ament = True,
)

ros2_cpp_binary(
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
    set_up_ament = True,
)

ros2_cpp_binary(
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
    set_up_ament = True,
)

ros2_py_binary(
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
    set_up_ament = True,
)

ros2_cpp_binary(
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
    set_up_ament = True,
)

filegroup(
    name = "ros2_nodes",
    srcs = [
        ":camera_publisher",
        ":camera_publisher_impl",
        ":encoder_publisher",
        ":encoder_publisher_impl",
        ":actuator_subscriber",
        ":actuator_subscriber_impl",
        ":mock_inference",
        ":mock_inference_impl",
        ":mock_inference_py",
        ":mock_inference_py_impl",
        ":operational_limit_calibration",
        ":operational_limit_calibration_impl",
    ],
    visibility = ["//visibility:public"],
)
