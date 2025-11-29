load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "node_generator",
    srcs = ["node_generator.cc"],
    hdrs = ["node_generator.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config:config_utils",
        "//ros2/proto:node_cc_proto",
        "//utils:status_macros",
        "@abseil-cpp//absl/container:flat_hash_map",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)
