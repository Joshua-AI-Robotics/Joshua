load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_library")

cc_library(
    name = "perception_factory",
    hdrs = ["perception_factory.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/comm/factory:comm_factory",
        "//robot/perception/camera:cv_camera",
        "//robot/perception/encoder:sts3215_encoder",
        "//robot/perception/lidar:lds01_driver",
        "//robot/perception/interfaces:perception_interface",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
    ],
)

py_library(
    name = "perception_factory_py",
    srcs = ["perception_factory.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/perception/camera:mock_camera_py",
        "//robot/perception/interfaces:perception_interface_py",
        "//robot/perception/proto:perception_py_proto",
    ],
)
