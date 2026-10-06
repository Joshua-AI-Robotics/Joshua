load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

# Concrete serial implementations are reachable only through CommFactory.
package(default_visibility = ["//robot/comm:__subpackages__"])

cc_library(
    name = "serial",
    srcs = ["serial.cc"],
    hdrs = ["serial.h"],
    deps = [
        "//robot/comm/interfaces:byte_stream",
        "//robot/comm/interfaces:message_framer",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@boost.asio",
        "@glog",
    ],
)

cc_library(
    name = "serial_message_transport",
    hdrs = ["serial_message_transport.h"],
    deps = [
        ":serial",
        "//robot/comm/interfaces:message_framer",
        "//robot/comm/interfaces:message_transport",
    ],
)

cc_test(
    name = "serial_test",
    srcs = ["serial_test.cc"],
    deps = [
        ":serial",
        "//robot/comm/interfaces:message_framer",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
        "@boost.asio",
        "@googletest//:gtest_main",
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
