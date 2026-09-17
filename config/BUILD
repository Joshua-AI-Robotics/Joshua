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
    name = "validation",
    srcs = ["validation.cc"],
    hdrs = ["validation.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:config_cc_proto",
        "//robot/board/factory:board_resolver",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "validation_test",
    srcs = ["validation_test.cc"],
    deps = [
        ":validation",
        "//config/proto:config_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)
