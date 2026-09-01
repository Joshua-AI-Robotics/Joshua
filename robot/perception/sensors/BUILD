load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

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

cc_test(
    name = "joint_position_sensor_test",
    srcs = ["joint_position_sensor_test.cc"],
    deps = [
        ":joint_position_sensor",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
