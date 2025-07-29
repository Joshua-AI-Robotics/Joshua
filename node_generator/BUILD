load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

cc_library(
    name = "node_generator",
    srcs = ["node_generator.cc"],
    hdrs = ["node_generator.h"],
    deps = [
        "//config:config_utils",
        "@glog//:glog",
    ],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "main",
    srcs = ["main.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    deps = [
        ":node_generator",
        "@glog//:glog",
        "@gflags//:gflags",
    ],
    visibility = ["//visibility:public"],
)
