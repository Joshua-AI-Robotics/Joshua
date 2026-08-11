load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "teensy_board",
    hdrs = ["teensy_board.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//firmware/common:joshua_wire_v1",
        "//robot/board/joshua_wire:joshua_wire_board",
        "//robot/board/proto:board_cc_proto",
    ],
)

cc_binary(
    name = "teensy_driver_smoke",
    srcs = ["teensy_driver_smoke.cc"],
    deps = [
        ":teensy_board",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/proto:comm_cc_proto",
        "@glog",
    ],
)

cc_test(
    name = "teensy_board_test",
    srcs = ["teensy_board_test.cc"],
    deps = [
        ":teensy_board",
        "//firmware/common:joshua_wire_v1",
        "//robot/board/frame:fake_frame_transport",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
