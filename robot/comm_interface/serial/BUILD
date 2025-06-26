load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "serial",
    srcs = ["serial.cc"],
    hdrs = ["serial.h"],
    deps = [
        "@local_deps//:boost_asio",
        "@com_github_google_glog//:glog",
    ],
    visibility = ["//visibility:public"],
)
