load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_python//python:defs.bzl", "py_test")

cc_library(
    name = "ros_client",
    srcs = ["ros_client.cc"],
    hdrs = ["ros_client.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//ros2:actuator_session",
        "//ros2/utils:qos_setting",
        "@protobuf",
        "@ros2_deps//:cpp_std_msgs",
        "@ros2_deps//:rclcpp",
    ],
)

cc_binary(
    name = "ros_bridge",
    srcs = ["ros_bridge.cc"],
    deps = [
        ":ros_client",
        "//config:config_utils",
        "//ros2:actuator_session",
        "@glog",
        "@protobuf",
        "@ros2_deps//:rclcpp",
    ],
)

py_test(
    name = "ros_bridge_test",
    srcs = [
        "client.py",
        "ros_bridge_test.py",
    ],
    args = [
        "$(location :ros_bridge)",
        "$(location //config/config_preset:example/teensy_hardware_api.pbtxt)",
    ],
    data = [
        ":ros_bridge",
        "//config/config_preset:example/teensy_hardware_api.pbtxt",
    ],
    imports = ["."],
)

py_test(
    name = "session_test",
    srcs = [
        "client.py",
        "session.py",
        "session_test.py",
    ],
    imports = ["."],
)
