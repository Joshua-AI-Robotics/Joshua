load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "perception_factory",
    hdrs = ["perception_factory.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/comm/factory:comm_factory",
        "//robot/perception/camera:cv_camera",
        "//robot/perception/encoder:sts3215_encoder",
        "//robot/perception/interfaces:perception_interface",
        "//robot/perception/lidar:lds01_driver",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
    ],
)
