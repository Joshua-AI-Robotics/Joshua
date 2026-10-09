load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "commands",
    srcs = ["src/joshua_commands.c"],
    hdrs = ["src/joshua_commands.h"],
    visibility = [
        "//robot/board/joshua_wire:__pkg__",
        "//robot/comm/ethercat:__pkg__",
    ],
    deps = ["//firmware/common:joshua_wire_commands"],
)

cc_test(
    name = "uart_transport_test",
    srcs = [
        "src/transport_uart.c",
        "src/transport_uart.h",
        "src/transport_uart_test.cc",
    ],
    deps = [
        "//firmware/common:serial_frame_assembler",
        "//firmware/testing:ti_uart",
        "@googletest//:gtest_main",
    ],
)
