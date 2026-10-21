load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "esp32_board",
    hdrs = ["esp32_board.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/board/joshua_wire:joshua_wire_board",
        "//robot/board/proto:board_cc_proto",
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
        "//robot/board/joshua_wire/testing:board_config",
        "//robot/board/joshua_wire/testing:fake_joshua_wire_transport",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/factory:comm_factory",
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
