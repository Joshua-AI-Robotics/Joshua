load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "joint_position_sensor",
    srcs = ["joint_position_sensor.cc"],
    hdrs = ["joint_position_sensor.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/board/interfaces:board_interfaces",
        "//robot/perception/interfaces:perception_interface",
        "//robot/perception/proto:perception_cc_proto",
        "//robot/perception/proto:perception_packet_cc_proto",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)
