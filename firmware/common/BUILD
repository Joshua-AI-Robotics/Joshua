load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

# Shared revision-4 core, profiles, runtimes and transport adapters.
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
    name = "joshua_wire_sensor_stream",
    srcs = ["joshua_wire_sensor_stream.c"],
    hdrs = ["joshua_wire_sensor_stream.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":joshua_wire",
        ":joshua_wire_profiles",
    ],
)

cc_library(
    name = "joshua_stream_subscription",
    srcs = ["joshua_stream_subscription.c"],
    hdrs = ["joshua_stream_subscription.h"],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [":joshua_wire_sensor_stream"],
)

cc_test(
    name = "joshua_wire_sensor_stream_test",
    srcs = ["joshua_wire_sensor_stream_test.cc"],
    deps = [
        ":joshua_wire_sensor_stream",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "joshua_stream_subscription_test",
    srcs = ["joshua_stream_subscription_test.cc"],
    deps = [
        ":joshua_stream_subscription",
        ":joshua_wire_endpoint",
        "@googletest//:gtest_main",
    ],
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

cc_library(
    name = "joshua_scalar_sensor",
    srcs = ["joshua_scalar_sensor.c"],
    hdrs = ["joshua_scalar_sensor.h"],
    copts = [
        "-x",
        "c",
        "-std=c11",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":joshua_device_runtime",
        ":joshua_stream_subscription",
        ":joshua_wire_resources",
    ],
)

cc_library(
    name = "joshua_watchdogs",
    srcs = ["joshua_watchdogs.c"],
    hdrs = ["joshua_watchdogs.h"],
    visibility = ["//visibility:public"],
    deps = [":joshua_wire_commands"],
)

cc_library(
    name = "joshua_mixed_serial",
    srcs = ["joshua_mixed_serial.c"],
    hdrs = ["joshua_mixed_serial.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":frame_transport",
        ":joshua_wire",
    ],
)

cc_library(
    name = "mixed_test_firmware",
    testonly = True,
    srcs = ["joshua_mixed_device.cpp"],
    hdrs = ["joshua_mixed_device.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":joshua_scalar_sensor",
        ":joshua_watchdogs",
        ":stepdir_test_firmware",
    ],
)

cc_test(
    name = "joshua_mixed_device_test",
    srcs = ["joshua_mixed_device_test.cc"],
    deps = [
        ":joshua_mixed_serial",
        ":mixed_test_firmware",
        ":serial_frame_transport",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "joshua_wire_peripheral",
    srcs = ["joshua_wire_peripheral.c"],
    hdrs = ["joshua_wire_peripheral.h"],
    visibility = ["//visibility:public"],
    deps = [":joshua_wire"],
)

cc_library(
    name = "joshua_peripheral_handler",
    srcs = ["joshua_peripheral_handler.c"],
    hdrs = ["joshua_peripheral_handler.h"],
    copts = [
        "-x",
        "c",
        "-std=c11",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":joshua_device_runtime",
        ":joshua_wire_peripheral",
        ":joshua_wire_resources",
    ],
)

cc_library(
    name = "peripheral_test_firmware",
    testonly = True,
    srcs = ["joshua_peripheral_device.cpp"],
    hdrs = ["joshua_peripheral_device.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":joshua_peripheral_handler",
        ":stepdir_test_firmware",
    ],
)

cc_test(
    name = "joshua_peripheral_handler_test",
    srcs = ["joshua_peripheral_handler_test.cc"],
    deps = [
        ":joshua_peripheral_handler",
        "@googletest//:gtest_main",
    ],
)

# Compile the actual adapter for both platform API shapes against recorded I/O.
cc_library(
    name = "arduino_teensy_peripheral_test_firmware",
    testonly = True,
    srcs = ["joshua_arduino_peripherals.cpp"],
    defines = ["JOSHUA_PERIPHERAL_REFERENCE"],
    deps = [
        ":peripheral_test_firmware",
        "//firmware/testing:arduino",
    ],
)

cc_library(
    name = "arduino_esp32_peripheral_test_firmware",
    testonly = True,
    srcs = ["joshua_arduino_peripherals.cpp"],
    defines = [
        "JOSHUA_PERIPHERAL_REFERENCE",
        "ESP32",
    ],
    deps = [
        ":peripheral_test_firmware",
        "//firmware/testing:arduino",
    ],
)

cc_test(
    name = "arduino_teensy_peripheral_test",
    srcs = ["joshua_arduino_peripherals_test.cc"],
    defines = ["EXPECTED_NATIVE_BOARD=1"],
    deps = [
        ":arduino_teensy_peripheral_test_firmware",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "arduino_esp32_peripheral_test",
    srcs = ["joshua_arduino_peripherals_test.cc"],
    defines = ["EXPECTED_NATIVE_BOARD=2"],
    deps = [
        ":arduino_esp32_peripheral_test_firmware",
        "@googletest//:gtest_main",
    ],
)
