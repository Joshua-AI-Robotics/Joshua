load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "serial",
    srcs = ["serial.cc"],
    hdrs = ["serial.h"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        "//robot/comm/interfaces:byte_stream",
        "//robot/comm/interfaces:legacy_message_transport",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@boost.asio",
        "@glog",
    ],
)

cc_binary(
    name = "am243_demo_smoke",
    srcs = ["am243_demo_smoke.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":serial",
        "//firmware/common:joshua_wire_v1",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@boost.asio",
    ],
)

cc_test(
    name = "serial_exchange_test",
    srcs = ["serial_exchange_test.cc"],
    deps = [
        ":serial",
        "//firmware/common:joshua_wire_v2",
        "@boost.asio",
        "@googletest//:gtest_main",
    ],
)
