load("@com_github_mvukov_rules_ros2//ros2:py_defs.bzl", "ros2_py_binary")
load("@com_github_mvukov_rules_ros2//ros2:cc_defs.bzl", "ros2_cpp_binary")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")
load("@rules_pkg//pkg:pkg.bzl", "pkg_tar")

package(default_visibility = ["//visibility:public"])

# GR00T N1.5 Inference Node
ros2_py_binary(
    name = "gr00t_n15_inference_node",
    srcs = ["gr00t_n15_inference.py"],
    main = "gr00t_n15_inference.py",
    deps = [
        "@ros2_rclpy//:rclpy",
        "@ros2_common_interfaces//:py_std_msgs",
        "@ros2_common_interfaces//:py_sensor_msgs",
        "@ros2_common_interfaces//:py_geometry_msgs",
        "@ros2_common_interfaces//:py_std_srvs",
        requirement("opencv-python"),
        requirement("numpy"),
        requirement("torch"),
        requirement("transformers"),
        requirement("huggingface-hub"),
        requirement("pillow"),
        requirement("protobuf"),
    ],
    data = [],
    tags = ["manual"],
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
        "@ros2_common_interfaces//:cpp_sensor_msgs",
        "@local_deps//:opencv",
        "//robot/perception/factory:perception_factory",
        "//config/proto:config_cc_proto",
        "//config:config_utils",
        "//robot/nexus/proto:nexus_packet_cc_proto",
    ],
)

ros2_cpp_binary(
    name = "encoder_publisher",
    srcs = ["encoder_publisher.cc"],
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
)

ros2_cpp_binary(
    name = "actuation_subscriber",
    srcs = ["actuation_subscriber.cc"],
    data = [
        "//config/config_preset:so100_with_example_ai",
    ],
    deps = [
        "@ros2_rclcpp//:rclcpp",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_common_interfaces//:cpp_sensor_msgs",
        "@ros2_common_interfaces//:cpp_geometry_msgs",
        "@ros2_common_interfaces//:cpp_std_srvs",
        "@gflags//:gflags",
        "//config/proto:config_cc_proto",
        "//config:config_utils",
        "//robot/actuation/factory:actuation_factory",
    ],
)
