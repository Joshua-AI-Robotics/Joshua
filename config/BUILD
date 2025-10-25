load("@rules_cc//cc:defs.bzl", "cc_library")

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
