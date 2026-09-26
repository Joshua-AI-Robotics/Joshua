load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

# Shared semantic definitions; neither frame version depends on the other.
cc_library(
    name = "joshua_wire_commands",
    hdrs = ["joshua_wire_commands.h"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "joshua_wire_commands_test",
    srcs = ["joshua_wire_commands_test.cc"],
    deps = [
        ":joshua_wire_commands",
        "@googletest//:gtest_main",
    ],
)

# Native builds of the same codecs compiled into MCU firmware.
# See README.md for the file map, test boundaries and migration adapters.
cc_library(
    name = "joshua_wire_v1",
    srcs = ["joshua_wire_v1.c"],
    hdrs = ["joshua_wire_v1.h"],
    visibility = ["//visibility:public"],
    deps = [":joshua_wire_commands"],
)

cc_test(
    name = "joshua_wire_v1_test",
    srcs = ["joshua_wire_v1_test.cc"],
    deps = [
        ":joshua_wire_v1",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "joshua_wire_v2",
    srcs = ["joshua_wire_v2.c"],
    hdrs = ["joshua_wire_v2.h"],
    visibility = ["//visibility:public"],
    deps = [":joshua_wire_commands"],
)

cc_library(
    name = "joshua_wire_v2_firmware_session",
    srcs = ["joshua_wire_v2_firmware_session.c"],
    hdrs = ["joshua_wire_v2_firmware_session.h"],
    visibility = ["//visibility:public"],
    deps = [":joshua_wire_v2"],
)

cc_test(
    name = "joshua_wire_v2_test",
    srcs = ["joshua_wire_v2_test.cc"],
    deps = [
        ":joshua_wire_serial_endpoint",
        ":joshua_wire_v1",
        ":joshua_wire_v2",
        ":joshua_wire_v2_firmware_session",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "joshua_wire_serial_endpoint",
    srcs = ["joshua_wire_serial_endpoint.c"],
    hdrs = ["joshua_wire_serial_endpoint.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":joshua_wire_v1",
        ":joshua_wire_v2_firmware_session",
    ],
)

filegroup(
    name = "stepdir_sources",
    srcs = [
        "backend_stepdir.cpp",
        "backend_stepdir.h",
        "joshua_stepdir_commands.cpp",
        "joshua_stepdir_commands.h",
    ],
    visibility = ["//firmware:__subpackages__"],
)
