load("@com_github_mvukov_rules_ros2//ros2:cc_defs.bzl", "ros2_cpp_binary")
load("@com_github_mvukov_rules_ros2//ros2:py_defs.bzl", "ros2_py_binary")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

ros2_cpp_binary(
    name = "simple_publisher",
    srcs = ["simple_publisher.cc"],
    deps = [
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_rclcpp//:rclcpp",
    ],
)

ros2_cpp_binary(
    name = "xbox_controller_publisher",
    srcs = ["xbox_controller_publisher.cc"],
    deps = [
        "//utils:xbox_controller",
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_rclcpp//:rclcpp",
    ],
)

ros2_cpp_binary(
    name = "camera_subscriber",
    srcs = ["camera_subscriber.cc"],
    deps = [
        "@local_deps//:opencv",
        "@ros2_common_interfaces//:cpp_sensor_msgs",
        "@ros2_rclcpp//:rclcpp",
    ],
)

ros2_cpp_binary(
    name = "keyboard_publisher",
    srcs = ["keyboard_publisher.cc"],
    deps = [
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_rclcpp//:rclcpp",
    ],
)

ros2_py_binary(
    name = "example_inference_node",
    srcs = ["example_inference_node.py"],
    data = [],
    main = "example_inference_node.py",
    tags = ["manual"],
    deps = [
        "@ros2_common_interfaces//:py_geometry_msgs",
        "@ros2_common_interfaces//:py_sensor_msgs",
        "@ros2_common_interfaces//:py_std_msgs",
        "@ros2_common_interfaces//:py_std_srvs",
        "@ros2_rclpy//:rclpy",
        requirement("opencv-python"),
        requirement("numpy"),
        requirement("torch"),
        requirement("transformers"),
        requirement("huggingface-hub"),
        requirement("pillow"),
        requirement("protobuf"),
    ],
)

ros2_cpp_binary(
    name = "mock_action_publisher",
    srcs = ["mock_action_publisher.cc"],
    deps = [
        "@ros2_common_interfaces//:cpp_std_msgs",
        "@ros2_rclcpp//:rclcpp",
    ],
)
