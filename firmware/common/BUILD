load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

# Shared semantic definitions; neither frame version depends on the other.
cc_library(
    name = "joshua_wire_commands",
    srcs = ["joshua_wire_commands.c"],
    hdrs = ["joshua_wire_commands.h"],
    includes = ["."],
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
cc_library(
    name = "joshua_wire_ethercat",
    hdrs = ["joshua_wire_ethercat.h"],
    visibility = ["//visibility:public"],
    deps = [":joshua_wire"],
)

# See README.md for the file map, test boundaries and migration adapters.
cc_library(
    name = "joshua_ethercat_profile",
    srcs = ["joshua_ethercat_profile.c"],
    hdrs = ["joshua_ethercat_profile.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":joshua_wire_ethercat",
        ":joshua_wire_firmware_session",
    ],
)

cc_library(
    name = "joshua_wire",
    srcs = ["joshua_wire.c"],
    hdrs = ["joshua_wire.h"],
    visibility = ["//visibility:public"],
    deps = [":joshua_wire_commands"],
)

cc_library(
    name = "joshua_wire_firmware_session",
    srcs = ["joshua_wire_firmware_session.c"],
    hdrs = ["joshua_wire_firmware_session.h"],
    visibility = ["//visibility:public"],
    deps = [":joshua_wire"],
)

cc_test(
    name = "joshua_wire_test",
    srcs = ["joshua_wire_test.cc"],
    deps = [
        ":joshua_wire",
        ":joshua_wire_endpoint",
        ":joshua_wire_firmware_session",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "joshua_wire_endpoint",
    srcs = ["joshua_wire_endpoint.c"],
    hdrs = ["joshua_wire_endpoint.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":joshua_wire_firmware_session",
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

cc_library(
    name = "frame_transport",
    hdrs = ["frame_transport.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "serial_frame_assembler",
    srcs = ["serial_frame_assembler.c"],
    hdrs = ["serial_frame_assembler.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":frame_transport",
        ":joshua_wire",
    ],
)

cc_test(
    name = "serial_frame_assembler_test",
    srcs = ["serial_frame_assembler_test.cc"],
    deps = [
        ":serial_frame_assembler",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "serial_frame_transport",
    srcs = ["serial_frame_transport.c"],
    hdrs = ["serial_frame_transport.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [":serial_frame_assembler"],
)

cc_test(
    name = "serial_frame_transport_test",
    srcs = ["serial_frame_transport_test.cc"],
    deps = [
        ":serial_frame_transport",
        "@googletest//:gtest_main",
    ],
)
