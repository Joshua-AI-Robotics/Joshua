load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "frame_transport",
    hdrs = ["frame_transport.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "serial_frame_transport",
    srcs = ["serial_frame_transport.cc"],
    hdrs = ["serial_frame_transport.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":frame_transport",
        "//robot/comm/serial",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "fake_frame_transport",
    testonly = True,
    hdrs = ["fake_frame_transport.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":frame_transport",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "serial_frame_transport_test",
    srcs = ["serial_frame_transport_test.cc"],
    deps = [
        ":serial_frame_transport",
        "//robot/comm/serial:fake_serial_transport",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
