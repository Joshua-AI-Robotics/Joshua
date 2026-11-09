load("@rules_python//python:defs.bzl", "py_binary")
load("//:requirements.bzl", "requirement")

py_binary(
    name = "record_scalar",
    srcs = ["record_scalar.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/perception/proto:perception_packet_py_proto",
        "//ros2/utils:ros_python_paths_py",
    ] + requirement("protobuf") + requirement("numpy"),
)
