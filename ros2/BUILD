# System ROS2 rules (using system-installed ROS2 libraries)
load("@rules_python//python:defs.bzl", "py_library")
load("//:requirements.bzl", "requirement")
load("//:system_ros2_rules.bzl", "ros2_cpp_binary", "ros2_py_binary")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "node_runner",
    hdrs = ["node_runner.h"],
    data = [
        "//config/config_preset:config_presets",
    ],
    deps = [
        "//config:config_utils",
        "//config/proto:config_cc_proto",
        "@ros2_deps//:cpp_std_msgs",
        "@ros2_deps//:rclcpp",
    ],
)

py_library(
    name = "node_runner_py",
    srcs = [
        "node_runner.py",
    ],
    deps = [
        "//config/proto:config_py_proto",
        "//ros2/proto:trajectory_py_proto",
        # Note: rclpy is available via system Python installation
        # Ensure ROS2 is sourced: source /opt/ros/humble/setup.bash
    ] + requirement("protobuf") + requirement("PyYAML"),
    visibility = ["//visibility:public"],
)

py_library(
    name = "ros2_type_resolver",
    srcs = [
        "ros2_type_resolver.py",
    ],
    deps = [
        "//ros2/proto:ros2_data_type_py_proto",
    ],
    visibility = ["//visibility:public"],
)


ros2_py_binary(
    name = "inference",
    srcs = [
        "inference.py",
    ],
    data = [
        "//config/config_preset:config_presets",
    ],
    main = "inference.py",
    set_up_ament = True,
    deps = [
        "//config/proto:config_py_proto",
        "//robot/action/proto:action_packet_py_proto",
        "//ros2:ros2_type_resolver",
        "//ros2:node_runner_py",
        "//ros2/proto:ros2_data_type_py_proto",
        "//ros2/proto:trajectory_py_proto",
        "//ros2/utils:packet_parser_py",
        "//ros2/utils:qos_setting_py",
        "//ai/models:model_registry",
    ] + requirement("numpy") + requirement("protobuf") + requirement("PyYAML"),
    visibility = ["//visibility:public"],
    # Note: rclpy is provided by system Python when ROS2 is sourced
    # Ensure: source /opt/ros/humble/setup.bash before running
)

py_library(
    name = "image_converter",
    srcs = [
        "image_converter.py",
    ],
    deps = [
    ] + requirement("numpy"),
    visibility = ["//visibility:public"],
)

ros2_cpp_binary(
    name = "camera_publisher",
    srcs = ["camera_publisher.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    set_up_ament = True,
    deps = [
        "//config/proto:config_cc_proto",
        "//robot/perception/factory:perception_factory",
        "//robot/perception/proto:perception_packet_cc_proto",
        "//ros2:node_runner",
        "//ros2/proto:node_cc_proto",
        "//ros2/utils:packet_parser",
        "//ros2/utils:qos_setting",
        "@ros2_deps//:cpp_sensor_msgs",
        "@ros2_deps//:cpp_std_msgs",
        "@ros2_deps//:rclcpp",
    ],
)

ros2_py_binary(
    name = "camera_publisher_py",
    srcs = [
        "camera_publisher.py",
    ],
    data = [
        "//config/config_preset:config_presets",
    ],
    main = "camera_publisher.py",
    set_up_ament = True,
    deps = [
        "//config/proto:config_py_proto",
        "//robot/perception/factory:perception_factory_py",
        "//robot/perception/proto:perception_packet_py_proto",
        "//ros2:node_runner_py",
        "//ros2/proto:node_py_proto",
        "//ros2/proto:ros2_data_type_py_proto",
        "//ros2/proto:trajectory_py_proto",
        "//ros2/utils:packet_parser_py",
        "//ros2/utils:qos_setting_py",
    ] + requirement("protobuf"),
)

ros2_cpp_binary(
    name = "encoder_publisher",
    srcs = ["encoder_publisher.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    set_up_ament = True,
    deps = [
        "//config/proto:config_cc_proto",
        "//robot/perception/factory:perception_factory",
        "//robot/perception/proto:perception_cc_proto",
        "//robot/perception/proto:perception_packet_cc_proto",
        "//ros2:node_runner",
        "//ros2/proto:node_cc_proto",
        "//ros2/utils:packet_parser",
        "//ros2/utils:qos_setting",
        "@ros2_deps//:cpp_std_msgs",
        "@ros2_deps//:rclcpp",
    ],
)

ros2_py_binary(
    name = "encoder_publisher_py",
    srcs = [
        "encoder_publisher.py",
    ],
    data = [
        "//config/config_preset:config_presets",
    ],
    main = "encoder_publisher.py",
    set_up_ament = True,
    deps = [
        "//config/proto:config_py_proto",
        "//robot/perception/factory:perception_factory_py",
        "//robot/perception/proto:perception_packet_py_proto",
        "//ros2:node_runner_py",
        "//ros2/proto:node_py_proto",
        "//ros2/proto:ros2_data_type_py_proto",
        "//ros2/proto:trajectory_py_proto",
        "//ros2/utils:packet_parser_py",
        "//ros2/utils:qos_setting_py",
    ] + requirement("protobuf"),
)

ros2_cpp_binary(
    name = "lidar_publisher",
    srcs = ["lidar_publisher.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    deps = [
        "@ros2_deps//:rclcpp",
        "@ros2_deps//:cpp_std_msgs",
        "@ros2_deps//:cpp_sensor_msgs",
        "//robot/perception/factory:perception_factory",
        "//config/proto:config_cc_proto",
        "//robot/perception/proto:perception_packet_cc_proto",
        "//robot/perception/proto:perception_cc_proto",
        "//ros2/proto:node_cc_proto",
        "//ros2/utils:packet_parser",
        "//ros2/utils:qos_setting",
        "//ros2:node_runner",
    ],
    set_up_ament = True,
)

ros2_py_binary(
    name = "lidar_publisher_py",
    srcs = [
        "lidar_publisher.py",
    ],
    data = [
        "//config/config_preset:config_presets",
    ],
    main = "lidar_publisher.py",
    set_up_ament = True,
    deps = [
        "//config/proto:config_py_proto",
        "//robot/perception/factory:perception_factory_py",
        "//robot/perception/proto:perception_packet_py_proto",
        "//ros2:node_runner_py",
        "//ros2/proto:node_py_proto",
        "//ros2/proto:ros2_data_type_py_proto",
        "//ros2/proto:trajectory_py_proto",
        "//ros2/utils:packet_parser_py",
        "//ros2/utils:qos_setting_py",
    ] + requirement("protobuf"),
)

ros2_cpp_binary(
    name = "actuator_subscriber",
    srcs = ["actuator_subscriber.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    set_up_ament = True,
    deps = [
        "//config/proto:config_cc_proto",
        "//robot/action/factory:action_factory",
        "//robot/action/proto:action_cc_proto",
        "//robot/action/proto:action_packet_cc_proto",
        "//ros2:node_runner",
        "//ros2/proto:node_cc_proto",
        "//ros2/utils:packet_parser",
        "//ros2/utils:qos_setting",
        "@abseil-cpp//absl/strings",
        "@protobuf//:protobuf",
        "@ros2_deps//:cpp_std_msgs",
        "@ros2_deps//:cpp_std_srvs",
        "@ros2_deps//:rclcpp",
    ],
)

ros2_py_binary(
    name = "actuator_subscriber_py",
    srcs = [
        "actuator_subscriber.py",
    ],
    data = [
        "//config/config_preset:config_presets",
    ],
    main = "actuator_subscriber.py",
    set_up_ament = True,
    deps = [
        "//config/proto:config_py_proto",
        "//robot/action/factory:action_factory_py",
        "//robot/action/proto:action_packet_py_proto",
        "//ros2:node_runner_py",
        "//ros2/proto:node_py_proto",
        "//ros2/proto:ros2_data_type_py_proto",
        "//ros2/proto:trajectory_py_proto",
        "//ros2/utils:packet_parser_py",
        "//ros2/utils:qos_setting_py",
    ] + requirement("protobuf"),
)

ros2_py_binary(
    name = "trajectory_publisher_py",
    srcs = [
        "trajectory_publisher.py",
    ],
    data = [
        "//config/config_preset:config_presets",
    ],
    main = "trajectory_publisher.py",
    set_up_ament = True,
    deps = [
        "//config/proto:config_py_proto",
        "//robot/action/proto:action_packet_py_proto",
        "//ros2:node_runner_py",
        "//ros2/proto:node_py_proto",
        "//ros2/proto:ros2_data_type_py_proto",
        "//ros2/proto:trajectory_py_proto",
        "//ros2/utils:packet_parser_py",
        "//ros2/utils:qos_setting_py",
    ] + requirement("protobuf"),
)

ros2_py_binary(
    name = "data_subscriber",
    srcs = [
        "data_subscriber.py",
    ],
    data = [
        "//config/config_preset:config_presets",
    ],
    main = "data_subscriber.py",
    set_up_ament = True,
    deps = [
        "//ai/train:data_store",
        "//config/proto:config_py_proto",
        "//ros2:node_runner_py",
        "//ros2:ros2_type_resolver",
        "//ros2/proto:node_py_proto",
        "//ros2/proto:trajectory_py_proto",
        "//ros2/utils:qos_setting_py",
        # Note: rclpy and std_msgs are available via system Python when ROS2 is sourced
    ] + requirement("protobuf"),
)

ros2_cpp_binary(
    name = "operational_limit_calibration",
    srcs = ["operational_limit_calibration.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    set_up_ament = True,
    deps = [
        "//config/proto:config_cc_proto",
        "//ros2:node_runner",
        "//ros2/utils:qos_setting",
        "@ros2_deps//:cpp_std_msgs",
        "@ros2_deps//:rclcpp",
    ],
)

genrule(
    name = "ros2_node_wrapper",
    srcs = ["ros2_node_wrapper_template.sh"],
    outs = ["ros2_node_wrapper.sh"],
    cmd = "cp $< $@ && chmod +x $@",
    visibility = ["//visibility:public"],
)
