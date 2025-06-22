load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "camera_factory",
    srcs = ["camera_factory.cc"],
    hdrs = ["camera_factory.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/perception/camera:cv_camera",
        "//robot/perception/interfaces:camera_interface",
        "//robot/config:robot_cc_proto",
    ],
)
