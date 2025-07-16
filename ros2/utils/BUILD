load("@com_github_mvukov_rules_ros2//ros2:cc_defs.bzl", "ros2_cpp_binary")

ros2_cpp_binary(
    name = "simple_publisher",
    srcs = ["simple_publisher.cc"],
    deps = [
        "@ros2_rclcpp//:rclcpp",
        "@ros2_common_interfaces//:cpp_std_msgs",
    ],
    visibility = ["//visibility:public"],
)

ros2_cpp_binary(
    name = "example_encoder_publisher",
    srcs = ["example_encoder_publisher.cc"],
    data = [
        "//config/config_preset:so100_with_example_ai",
    ],
    deps = [
        "@ros2_rclcpp//:rclcpp",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "//robot/perception/factory:perception_factory",
        "//config/proto:config_cc_proto",
        "//config:config_utils",
        "//robot/nexus/proto:nexus_packet_cc_proto",
    ],
    visibility = ["//visibility:public"],
)

ros2_cpp_binary(
    name = "xbox_controller_publisher",
    srcs = ["xbox_controller_publisher.cc"],
    deps = [
        "@ros2_rclcpp//:rclcpp",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "//utils:xbox_controller",
    ],
    visibility = ["//visibility:public"],
)

ros2_cpp_binary(
    name = "camera_publisher",
    srcs = ["camera_publisher.cc"],
    data = [
        "//config/config_preset:so100_with_example_ai",
    ],
    deps = [
        "@ros2_rclcpp//:rclcpp",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "//robot/perception/factory:perception_factory",
        "//config/proto:config_cc_proto",
        "//config:config_utils",
        "//robot/nexus/proto:nexus_packet_cc_proto",
    ],
    visibility = ["//visibility:public"],
)

ros2_cpp_binary(
    name = "camera_subscriber",
    srcs = ["camera_subscriber.cc"],
    deps = [
        "@ros2_rclcpp//:rclcpp",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@local_deps//:opencv",
    ],
    visibility = ["//visibility:public"],
)