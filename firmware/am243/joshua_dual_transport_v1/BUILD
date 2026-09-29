# Builds the AM243 software-only command handler/profile for native host/session
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

cc_library(
    name = "ethercat_profile",
    srcs = ["src/joshua_ethercat_profile.c"],
    hdrs = ["src/joshua_ethercat_profile.h"],
    visibility = [
        "//robot/board/joshua_wire:__pkg__",
        "//robot/comm/ethercat:__pkg__",
    ],
    deps = [
        ":commands",
        "//firmware/common:joshua_wire_ethercat",
        "//firmware/common:joshua_wire_v2_firmware_session",
    ],
)
