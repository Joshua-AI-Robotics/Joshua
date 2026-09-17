load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "node_generator",
    srcs = ["node_generator.cc"],
    hdrs = ["node_generator.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":validation",
        "//config:config_utils",
        "//ros2/proto:node_cc_proto",
        "//utils:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
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
