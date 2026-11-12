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
        "//firmware/common:joshua_actuator_handler",
        "//firmware/common:joshua_device_runtime",
        "//firmware/common:joshua_wire_endpoint",
        "//firmware/common:joshua_wire_resources",
        "//firmware/common:serial_frame_transport",
        "//firmware/common:stepdir_test_firmware",
        "//firmware/testing:arduino",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "mixed_scalar_test",
    srcs = [
        "mixed_scalar_test.cc",
        "src/adc_reference.cpp",
        "src/adc_reference.h",
        "src/main.cpp",
        "src/transport_serial.cpp",
        "src/transport_serial.h",
    ],
    defines = [
        "JOSHUA_TRANSPORT_SERIAL",
        "JOSHUA_MIXED_ADC_REFERENCE",
    ],
    includes = ["src"],
    deps = [
        "//firmware/common:joshua_mixed_serial",
        "//firmware/common:joshua_wire_stream",
        "//firmware/common:mixed_test_firmware",
        "//firmware/common:serial_frame_transport",
        "//firmware/testing:arduino",
        "@googletest//:gtest_main",
    ],
)
