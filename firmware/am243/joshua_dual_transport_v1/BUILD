# Builds the AM243 software-only command handler for native host/session
# tests without the TI SDK. The MCU firmware image is built by Makefile instead.
load("@rules_cc//cc:defs.bzl", "cc_library")

# Compiles the actual software-only command handler without the TI SDK.
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
