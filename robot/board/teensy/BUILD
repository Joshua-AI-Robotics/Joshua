load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "teensy_board",
    srcs = ["teensy_board.cc"],
    hdrs = ["teensy_board.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//firmware/common:joshua_wire_v1",
        "//robot/board/frame:frame_transport",
        "//robot/board/frame:serial_frame_transport",
        "//robot/board/interfaces:board_interfaces",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/factory:comm_factory",
        "//robot/comm/proto:comm_cc_proto",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
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
