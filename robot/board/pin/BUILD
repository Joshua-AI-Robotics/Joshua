load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "pin_source",
    srcs = [
        "pin_source.cc",
        "profiles/jetson_orin_nano.cc",
        "profiles/pin_source_helpers.h",
        "profiles/pin_sources.h",
        "profiles/raspberry_pi5.cc",
    ],
    hdrs = ["pin_source.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "pin_resource",
    srcs = ["pin_resource.cc"],
    hdrs = ["pin_resource.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":pin_source",
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
    deps = [":pin_source"],
)

cc_test(
    name = "pin_source_test",
    srcs = ["pin_source_test.cc"],
    deps = [
        ":pin_source",
        "@googletest//:gtest_main",
    ],
)
