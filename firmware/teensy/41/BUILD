# Builds native JoshuaWire tests from the actual Teensy dispatch, serial framing and
# STEP/DIR sources with fake Arduino I/O. PlatformIO builds the MCU artifacts.
load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "serial_test",
    srcs = [
        "src/main.cpp",
        "src/transport_serial.cpp",
        "src/transport_serial.h",
        "//firmware/testing:serial_firmware_test.cc",
    ],
    defines = [
        "JOSHUA_TRANSPORT_SERIAL",
        "EXPECTED_BOARD_ID=2",
    ],
    includes = ["src"],
    deps = [
        "//firmware/common:joshua_device_runtime",
        "//firmware/common:joshua_wire_endpoint",
        "//firmware/common:joshua_wire_resources",
        "//firmware/common:serial_frame_transport",
        "//firmware/common:stepdir_test_firmware",
        "//firmware/testing:arduino",
        "@googletest//:gtest_main",
    ],
)
