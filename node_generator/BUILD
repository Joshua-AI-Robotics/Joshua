load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

cc_binary(
    name = "main",
    srcs = ["main.cc"],
    data = [
        "//config/config_preset:so100_with_example_ai",
    ],
    deps = [
        "//config:config_utils",
        "@glog//:glog",
        "@gflags//:gflags",
    ],
    visibility = ["//visibility:public"],
)
