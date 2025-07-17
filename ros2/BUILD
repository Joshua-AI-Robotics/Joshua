load("@com_github_mvukov_rules_ros2//ros2:py_defs.bzl", "ros2_py_binary")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

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
