load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "software_backend",
    srcs = ["src/am243_software_backend.c"],
    hdrs = ["src/am243_software_backend.h"],
    visibility = [
        "//robot/board:__subpackages__",
        "//robot/comm/ethercat:__pkg__",
    ],
    deps = [
        "//firmware/common:joshua_actuator_handler",
        "//firmware/common:joshua_device_runtime",
        "//firmware/common:joshua_wire_commands",
        "//firmware/common:joshua_wire_profiles",
    ],
)

cc_test(
    name = "uart_transport_test",
    srcs = [
        "src/transport_uart.c",
        "src/transport_uart.h",
        "src/transport_uart_test.cc",
    ],
    deps = [
        "//firmware/common:serial_frame_transport",
        "//firmware/testing:ti_uart",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "ethercat_ti_native",
    testonly = True,
    srcs = [
        "src/joshua_ethercat_ti.c",
    ],
    hdrs = ["src/joshua_ethercat_ti.h"],
    # The repo uses g++ as CC; compile the actual firmware bridge as C, as TI does.
    copts = [
        "-x",
        "c",
        "-std=c11",
    ],
    defines = [
        "JOSHUA_COMM_WATCHDOG_US=20000",
        "JOSHUA_TARGET_WATCHDOG_US=10000",
    ],
    deps = [
        ":software_backend",
        "//firmware/common:joshua_ethercat_profile",
        "//firmware/testing:ti_ethercat",
    ],
)

cc_test(
    name = "ethercat_ti_test",
    srcs = ["src/joshua_ethercat_ti_test.cc"],
    deps = [
        ":ethercat_ti_native",
        ":software_backend",
        "//firmware/common:joshua_ethercat_profile",
        "//firmware/testing:ti_ethercat",
        "@googletest//:gtest_main",
    ],
)
