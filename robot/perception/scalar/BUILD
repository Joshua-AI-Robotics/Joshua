load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "board_scalar_sensor",
    hdrs = ["board_scalar_sensor.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/board/interfaces:board_interfaces",
        "//robot/perception/interfaces:perception_interface",
        "//utils:status_macros",
    ],
)
