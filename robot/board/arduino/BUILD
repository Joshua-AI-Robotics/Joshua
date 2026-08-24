load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "arduino_board",
    srcs = ["arduino_board.cc"],
    hdrs = ["arduino_board.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//firmware/common:joshua_wire_v1",
        "//robot/board/joshua_wire:joshua_wire_board",
        "//robot/board/proto:board_cc_proto",
        "//utils:status_macros",
    ],
)

cc_binary(
    name = "arduino_driver_smoke",
    srcs = ["arduino_driver_smoke.cc"],
    deps = [
        ":arduino_board",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/proto:comm_cc_proto",
        "@glog",
    ],
)

cc_test(
    name = "arduino_board_test",
    srcs = ["arduino_board_test.cc"],
    deps = [
        ":arduino_board",
        "//firmware/common:joshua_wire_v1",
        "//robot/board/frame:fake_frame_transport",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
