load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "node_generator",
    srcs = ["node_generator.cc"],
    hdrs = ["node_generator.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config:config_utils",
        "//config:validation",
        "//ros2/proto:node_cc_proto",
        "//utils:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_test(
    name = "monitor_test",
    srcs = ["monitor_test.cc"],
    deps = [
        ":node_generator",
        "@googletest//:gtest_main",
    ],
)
