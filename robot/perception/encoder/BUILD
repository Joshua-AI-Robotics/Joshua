load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "encoder",
    srcs = ["encoder.cc"],
    hdrs = ["encoder.h"],
    deps = [
        "//robot/perception/interfaces:encoder_interface",
        "@com_github_google_glog//:glog",
    ],
    visibility = ["//visibility:public"],
) 