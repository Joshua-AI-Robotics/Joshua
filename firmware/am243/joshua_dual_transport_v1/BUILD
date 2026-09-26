# Builds the AM243 software-only serial command handler for native host/session
# tests without the TI SDK. The MCU firmware image is built by Makefile instead.
load("@rules_cc//cc:defs.bzl", "cc_library")

# Compiles the actual software-only serial command handler without the TI SDK.
cc_library(
    name = "serial_commands",
    srcs = ["src/joshua_serial_commands.c"],
    hdrs = ["src/joshua_serial_commands.h"],
    visibility = ["//robot/board/joshua_wire:__pkg__"],
    deps = ["//firmware/common:joshua_wire_serial_endpoint"],
)
