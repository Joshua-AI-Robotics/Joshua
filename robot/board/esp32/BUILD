load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "esp32_board",
    srcs = ["esp32_board.cc"],
    hdrs = ["esp32_board.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//firmware/common:joshua_wire_v1",
        "//robot/board/joshua_wire:joshua_wire_board",
        "//robot/board/proto:board_cc_proto",
        "//utils:status_macros",
    ],
)

cc_binary(
    name = "esp32_driver_smoke",
    srcs = ["esp32_driver_smoke.cc"],
    deps = [
        ":esp32_board",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/proto:comm_cc_proto",
        "@glog",
    ],
)

cc_test(
    name = "esp32_board_test",
    srcs = ["esp32_board_test.cc"],
    deps = [
        ":esp32_board",
        "//firmware/common:joshua_wire_v1",
        "//robot/board/frame:fake_frame_transport",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
