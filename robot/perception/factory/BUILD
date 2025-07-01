load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "perception_factory",
    srcs = ["perception_factory.cc"],
    hdrs = ["perception_factory.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/perception/camera:cv_camera",
        "//robot/perception/encoder:sts3215_encoder",
        "//robot/perception/interfaces:perception_interface",
        "//robot/config:robot_cc_proto",
    ],
)
