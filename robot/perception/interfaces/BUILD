load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "perception_interface",
    hdrs = ["perception_interface.h"],
    deps = [
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "camera_interface",
    hdrs = ["camera_interface.h"],
    deps = [
        ":perception_interface",
        "//config/proto:robot_cc_proto",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "encoder_interface",
    hdrs = ["encoder_interface.h"],
    deps = [
        ":perception_interface",
    ],
    visibility = ["//visibility:public"],
)
