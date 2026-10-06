# Exposes the test-only Arduino API substitute and shared firmware test source
# to native board tests. These helpers are not part of MCU firmware artifacts.
load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "arduino",
    testonly = True,
    hdrs = ["Arduino.h"],
    includes = ["."],
    visibility = ["//firmware:__subpackages__"],
)

exports_files(
    ["serial_firmware_test.cc"],
    visibility = ["//firmware:__subpackages__"],
)

cc_library(
    name = "ti_uart",
    testonly = True,
    hdrs = [
        "ti_sdk/drivers/uart.h",
        "ti_sdk/kernel/dpl/ClockP.h",
    ],
    includes = ["ti_sdk"],
    visibility = ["//firmware/am243/joshua_dual_transport:__pkg__"],
)
