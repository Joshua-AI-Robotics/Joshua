# System ROS2 rules (using system-installed ROS2 libraries)
load("//:system_ros2_rules.bzl", "ros2_cpp_binary", "ros2_py_binary")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

ros2_cpp_binary(
    name = "simple_publisher",
    srcs = ["simple_publisher.cc"],
    deps = [
        "@ros2_deps//:cpp_std_msgs",
        "@ros2_deps//:rclcpp",
    ],
)

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
    name = "example_inference_node",
    srcs = ["example_inference_node.py"],
    data = [],
    main = "example_inference_node.py",
    tags = ["manual"],
    deps = [
        # Note: Python ROS2 messages and rclpy are available via system Python installation
        # Ensure ROS2 is sourced: source /opt/ros/humble/setup.bash
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
