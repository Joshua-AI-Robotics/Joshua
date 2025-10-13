load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "config_utils",
    hdrs = ["config_utils.h"],
    deps = [
        "@glog//:glog",
        "@abseil-cpp//absl/status:status",
        "//config/proto:config_cc_proto",
    ],
    visibility = ["//visibility:public"],
)

