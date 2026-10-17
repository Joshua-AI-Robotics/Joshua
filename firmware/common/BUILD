load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "joshua_wire_commands",
    srcs = ["joshua_wire_commands.c"],
    hdrs = [
        "joshua_wire_commands.h",
    ],
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "joshua_wire_commands_test",
    srcs = ["joshua_wire_commands_test.cc"],
    deps = [
        ":joshua_wire_commands",
        ":joshua_wire_profiles",
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
        ":joshua_wire_ethercat",
        ":joshua_wire_firmware_session",
        ":joshua_wire_profiles",
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

cc_library(
    name = "joshua_wire_profiles",
    srcs = ["joshua_wire_profiles.c"],
    hdrs = [
        "joshua_wire_actuator.h",
        "joshua_wire_io.h",
        "joshua_wire_profiles.h",
        "joshua_wire_sensor.h",
    ],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [":joshua_wire_commands"],
)

cc_library(
    name = "joshua_wire_resources",
    srcs = ["joshua_wire_resources.c"],
    hdrs = ["joshua_wire_resources.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [":joshua_wire_commands"],
)

cc_test(
    name = "joshua_wire_mixed_device_test",
    srcs = ["joshua_wire_mixed_device_test.cc"],
    deps = [
        ":joshua_wire_endpoint",
        ":joshua_wire_profiles",
        ":joshua_wire_resources",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "joshua_device_runtime",
    srcs = ["joshua_device_runtime.c"],
    hdrs = ["joshua_device_runtime.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":joshua_wire",
        ":joshua_wire_profiles",
    ],
)

cc_library(
    name = "joshua_wire_stream",
    srcs = ["joshua_wire_stream.c"],
    hdrs = ["joshua_wire_stream.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [":joshua_wire"],
)

# Compiles the production STEP/DIR application against native GPIO/clock hooks.
# The MCU images build these same sources with their real Arduino framework.
cc_library(
    name = "stepdir_test_firmware",
    testonly = True,
    srcs = [
        "backend_stepdir.cpp",
        "joshua_stepdir_device.cpp",
    ],
    hdrs = [
        "backend_stepdir.h",
        "joshua_stepdir_device.h",
        "stepdir_channel.h",
    ],
    includes = ["."],
    visibility = [
        "//firmware:__subpackages__",
        "//robot/board:__subpackages__",
    ],
    deps = [
        ":joshua_actuator_handler",
        ":joshua_device_runtime",
        ":joshua_wire_endpoint",
        ":joshua_wire_resources",
        "//firmware/testing:arduino",
    ],
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
        ":joshua_wire_stream",
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

cc_library(
    name = "joshua_ethercat_profile",
    srcs = ["joshua_ethercat_profile.c"],
    hdrs = ["joshua_ethercat_profile.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":joshua_wire_ethercat",
        ":joshua_wire_firmware_session",
        ":joshua_wire_profiles",
    ],
)

cc_library(
    name = "joshua_actuator_handler",
    srcs = ["joshua_actuator_handler.c"],
    hdrs = ["joshua_actuator_handler.h"],
    # Exercise the portable handler as C, including in native C++ test builds.
    copts = [
        "-x",
        "c",
        "-std=c11",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":joshua_device_runtime",
        ":joshua_wire_profiles",
        ":joshua_wire_resources",
    ],
)

cc_test(
    name = "joshua_actuator_handler_test",
    srcs = ["joshua_actuator_handler_test.cc"],
    deps = [
        ":joshua_actuator_handler",
        ":joshua_wire_endpoint",
        "@googletest//:gtest_main",
    ],
)
