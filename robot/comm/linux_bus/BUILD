load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "bus_validation",
    hdrs = ["bus_validation.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
    ],
)

cc_library(
    name = "linux_bus",
    srcs = ["linux_bus.cc"],
    hdrs = ["linux_bus.h"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        ":bus_validation",
        "//robot/comm/interfaces:peripheral_transports",
        "//utils:status_macros",
    ],
)

cc_test(
    name = "linux_bus_test",
    srcs = ["linux_bus_test.cc"],
    deps = [
        ":linux_bus",
        "@googletest//:gtest_main",
    ],
)
