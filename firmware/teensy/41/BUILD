# Builds native v1/v2 tests from the actual Teensy dispatch, serial framing and
# STEP/DIR sources with fake Arduino I/O. PlatformIO builds the MCU artifacts.
load("@rules_cc//cc:defs.bzl", "cc_test")

[
    cc_test(
        name = "serial_v%d_test" % version,
        srcs = [
            "src/channel_table.c",
            "src/channel_table.h",
            "src/main.cpp",
            "src/transport_serial.cpp",
            "src/transport_serial.h",
            "//firmware/common:stepdir_sources",
            "//firmware/testing:serial_firmware_test.cc",
        ],
        defines = [
            "JOSHUA_WIRE_VERSION=%d" % version,
            "EXPECTED_BOARD_ID=2",
        ],
        includes = ["src"],
        deps = [
            "//firmware/common:joshua_wire_serial_endpoint",
            "//firmware/testing:arduino",
            "@googletest//:gtest_main",
        ],
    )
    for version in [
        1,
        2,
    ]
]
