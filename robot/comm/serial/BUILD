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
    name = "joshua_wire_serial_transport",
    srcs = ["joshua_wire_serial_transport.cc"],
    hdrs = ["joshua_wire_serial_transport.h"],
    implementation_deps = ["//firmware/common:joshua_wire"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        ":serial",
        "//robot/comm/interfaces:message_transport",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "fixed_length_serial_transport",
    hdrs = ["fixed_length_serial_transport.h"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        ":serial",
        "//robot/comm/interfaces:legacy_message_transport",
    ],
)

cc_library(
    name = "serial_config_validation",
    hdrs = ["serial_config_validation.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
    ],
)

cc_test(
    name = "serial_exchange_test",
    srcs = ["serial_exchange_test.cc"],
    deps = [
        ":fixed_length_serial_transport",
        ":joshua_wire_serial_transport",
        ":serial",
        "//firmware/common:joshua_wire",
        "//robot/comm/interfaces:legacy_message_transport",
        "@boost.asio",
        "@googletest//:gtest_main",
    ],
)
