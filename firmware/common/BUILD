load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

# The shared artifact between host boards and Joshua-authored MCU firmware
# (docs/BOARD_LAYER_RFC.md §7.2/§7.3). This cc_library is the host build of
# joshua_wire_v1.{h,c}; the same two files are compiled again for each
# firmware target via its PlatformIO project (e.g.
# firmware/teensy/41/platformio.ini), never via Bazel cross-compilation —
# same source, two separate toolchains, one repo commit keeping them in
# sync.
cc_library(
    name = "joshua_wire_v1",
    srcs = ["joshua_wire_v1.c"],
    hdrs = ["joshua_wire_v1.h"],
    visibility = ["//visibility:public"],
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

cc_library(
    name = "joshua_wire_ethercat",
    hdrs = ["joshua_wire_ethercat.h"],
    visibility = ["//visibility:public"],
    deps = [":joshua_wire"],
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
