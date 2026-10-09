load("@rules_cc//cc:defs.bzl", "cc_library")

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
