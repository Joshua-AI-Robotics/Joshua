load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "serial",
    srcs = ["serial.cc"],
    hdrs = ["serial.h"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        "//robot/comm/interfaces:byte_stream",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
        "@boost.asio",
        "@glog",
    ],
)

cc_library(
    name = "framed_serial_transport",
    srcs = ["framed_serial_transport.cc"],
    hdrs = ["framed_serial_transport.h"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        ":serial",
        "//robot/comm/interfaces:legacy_message_transport",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "serial_exchange_test",
    srcs = ["serial_exchange_test.cc"],
    deps = [
        ":framed_serial_transport",
        ":serial",
        "//firmware/common:joshua_wire",
        "@boost.asio",
        "@googletest//:gtest_main",
    ],
)
