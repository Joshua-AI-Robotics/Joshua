load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "pin_resource",
    srcs = ["pin_resource.cc"],
    hdrs = ["pin_resource.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/board/proto:board_cc_proto",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "pin_resource_test",
    srcs = ["pin_resource_test.cc"],
    deps = [
        ":pin_resource",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "describe_pins",
    srcs = ["describe_pins.cc"],
    deps = [":pin_resource"],
)
