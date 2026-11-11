load("@rules_python//python:defs.bzl", "py_binary", "py_test")
load("//:requirements.bzl", "requirement")

py_binary(
    name = "record_scalar",
    srcs = ["record_scalar.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:config_py_proto",
        "//robot/perception/proto:perception_packet_py_proto",
        "//ros2/utils:ros_python_paths_py",
        "//utils:robot_time_py",
    ] + requirement("protobuf") + requirement("numpy"),
)

py_test(
    name = "record_scalar_test",
    srcs = [
        "record_scalar.py",
        "record_scalar_test.py",
    ],
    deps = [
        "//config/proto:config_py_proto",
        "//robot/perception/proto:perception_packet_py_proto",
        "//ros2/utils:ros_python_paths_py",
        "//utils:robot_time_py",
    ] + requirement("protobuf") + requirement("numpy"),
)
