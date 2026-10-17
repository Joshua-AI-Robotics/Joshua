# System ROS2 rules (using system-installed ROS2 libraries)
load("@rules_python//python:defs.bzl", "py_library", "py_test")
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
        "//config:validation",
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
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:config_py_proto",
        "//ros2/proto:trajectory_py_proto",
        "//ros2/utils:ros_python_paths_py",
        # rclpy is provided by the selected Docker image's ROS2 installation.
    ] + requirement("protobuf") + requirement("PyYAML"),
)

py_library(
    name = "ros2_type_resolver",
    srcs = [
        "ros2_type_resolver.py",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//ros2/proto:ros2_data_type_py_proto",
    ],
)

ros2_py_binary(
    name = "inference",
    srcs = [
        "inference.py",
    ],
    data = [
        "//ai/inference:inference_launcher",
        "//ai/models:manifests",
        "//ai/models:sources",
        "//config/config_preset:config_presets",
    ],
    main = "inference.py",
    set_up_ament = True,
    visibility = ["//visibility:public"],
    deps = [
        "//ai/inference:inference_launcher",
        "//config/proto:config_py_proto",
        "//ros2:node_runner_py",
        "//ros2/proto:trajectory_py_proto",
    ] + requirement("numpy") + requirement("protobuf") + requirement("PyYAML"),
    # rclpy is provided by the selected Docker image's ROS2 installation.
)

py_library(
    name = "image_converter",
    srcs = [
        "image_converter.py",
    ],
    visibility = ["//visibility:public"],
    deps = [
    ] + requirement("numpy"),
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

ros2_cpp_binary(
    name = "position_publisher",
    srcs = ["position_publisher.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    set_up_ament = True,
    deps = [
        "//config/proto:config_cc_proto",
        "//robot/perception/factory:perception_factory",
        "//ros2:node_runner",
        "//ros2/utils:packet_parser",
        "//ros2/utils:qos_setting",
        "@ros2_deps//:rclcpp",
    ],
)

ros2_cpp_binary(
    name = "lidar_publisher",
    srcs = ["lidar_publisher.cc"],
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
        "@ros2_deps//:cpp_sensor_msgs",
        "@ros2_deps//:cpp_std_msgs",
        "@ros2_deps//:rclcpp",
    ],
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
        "@glog",
        "@protobuf",
        "@ros2_deps//:cpp_std_msgs",
        "@ros2_deps//:cpp_std_srvs",
        "@ros2_deps//:rclcpp",
    ],
)

ros2_py_binary(
    name = "trajectory_publisher",
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

genrule(
    name = "ros2_node_wrapper",
    srcs = ["ros2_node_wrapper_template.sh"],
    outs = ["ros2_node_wrapper.sh"],
    cmd = "cp $< $@ && chmod +x $@",
    visibility = ["//visibility:public"],
)

cc_test(
    name = "node_runner_test",
    srcs = ["node_runner_test.cc"],
    deps = [
        ":node_runner",
        "//config/proto:config_cc_proto",
        "@googletest//:gtest_main",
        "@ros2_deps//:rclcpp",
    ],
)

ros2_cpp_binary(
    name = "audio_publisher",
    srcs = ["audio_publisher.cc"],
    deps = [
        "//config/proto:config_cc_proto",
        "//robot/perception/factory:perception_factory",
        "//ros2:node_runner",
        "//ros2/utils:qos_setting",
        "@ros2_deps//:cpp_std_msgs",
    ],
)

py_library(
    name = "monitor_common",
    srcs = ["monitor_common.py"],
    deps = ["//ros2/proto:ros2_data_type_py_proto"],
)

ros2_py_binary(
    name = "yamnet_node",
    srcs = ["yamnet_node.py"],
    deps = [
        ":monitor_common",
        ":node_runner_py",
        "//ai/models/yamnet:processor",
        "//ai/proto:ai_model_py_proto",
        "//robot/perception/proto:perception_packet_py_proto",
        "//ros2/proto:node_py_proto",
        "//ros2/proto:ros2_data_type_py_proto",
    ],
)

ros2_py_binary(
    name = "video_inference_node",
    srcs = ["video_inference_node.py"],
    deps = [
        ":monitor_common",
        ":node_runner_py",
        "//ai/models/video_detection:processor",
        "//ai/proto:ai_model_py_proto",
        "//ros2/proto:node_py_proto",
        "//ros2/proto:ros2_data_type_py_proto",
    ],
)

ros2_py_binary(
    name = "av_check",
    srcs = ["av_check.py"],
    deps = [
        ":monitor_common",
        ":node_runner_py",
        "//ai/models/video_detection:processor",
        "//ai/models/yamnet:processor",
        "//robot/perception/proto:perception_packet_py_proto",
        "//robot/perception/proto:perception_py_proto",
    ] + requirement("pillow") + requirement("numpy"),
)

py_test(
    name = "monitor_processor_test",
    srcs = ["monitor_processor_test.py"],
    deps = [
        "//ai/models/video_detection:processor",
        "//ai/models/yamnet:processor",
        "//robot/perception/proto:perception_packet_py_proto",
    ] + requirement("numpy"),
)

py_test(
    name = "monitor_pipeline_test",
    size = "medium",
    srcs = ["monitor_pipeline_test.py"],
    data = [
        ":video_inference_node",
        ":yamnet_node",
        "//config/config_preset:config_presets",
    ],
    deps = [
        ":monitor_common",
        "//config/proto:config_py_proto",
        "//robot/perception/proto:perception_packet_py_proto",
    ] + requirement("protobuf") + requirement("numpy") + requirement("PyYAML"),
)

ros2_py_binary(
    name = "monitor_selftest",
    srcs = ["monitor_processor_test.py"],
    main = "monitor_processor_test.py",
    deps = [
        "//ai/models/video_detection:processor",
        "//ai/models/yamnet:processor",
        "//robot/perception/proto:perception_packet_py_proto",
    ] + requirement("numpy"),
)

ros2_py_binary(
    name = "media_stream_node",
    srcs = ["//ros2/utils:media_stream_node.py"],
    main = "//ros2/utils:media_stream_node.py",
    deps = [
        ":node_runner_py",
        "//robot/perception/proto:perception_packet_py_proto",
        "//ros2/proto:node_py_proto",
        "//ros2/proto:ros2_data_type_py_proto",
        "//ros2/utils:media_stream",
    ] + requirement("numpy"),
)

ros2_py_binary(
    name = "home_assistant_setup",
    srcs = ["//ros2/utils:home_assistant_setup.py"],
    data = ["//deploy/home_assistant:templates"],
    main = "//ros2/utils:home_assistant_setup.py",
    deps = ["//config/proto:config_py_proto"] + requirement("protobuf"),
)

ros2_py_binary(
    name = "notification_node",
    srcs = ["//ros2/utils:notification_node.py"],
    main = "//ros2/utils:notification_node.py",
    deps = [
        ":node_runner_py",
        "//ros2/proto:node_py_proto",
        "//ros2/proto:ros2_data_type_py_proto",
        "//ros2/utils:notification",
    ] + requirement("numpy"),
)
