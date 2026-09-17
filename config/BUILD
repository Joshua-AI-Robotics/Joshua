load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "config_utils",
    hdrs = ["config_utils.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:config_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_library(
    name = "perception_validation",
    srcs = ["perception_validation.cc"],
    hdrs = ["perception_validation.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/board/factory:board_resolver",
        "//robot/perception/factory:sensor_config",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "perception_validation_test",
    srcs = ["perception_validation_test.cc"],
    deps = [
        ":perception_validation",
        "//config/proto:robot_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)
