load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

# An executable proposal, deliberately outside the production PlatformIO library.
cc_library(
    name = "device_contract",
    testonly = True,
    srcs = ["device_contract.c"],
    hdrs = ["device_contract.h"],
    deps = ["//firmware/common:joshua_wire"],
)

cc_test(
    name = "mixed_device_test",
    srcs = ["mixed_device_test.cc"],
    deps = [
        ":device_contract",
        "//firmware/common:joshua_wire_commands",
        "//firmware/common:joshua_wire_endpoint",
        "@googletest//:gtest_main",
    ],
)
