load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "serial",
    srcs = ["serial.cc"],
    hdrs = ["serial.h"],
    deps = [
        "@boost.asio",
        "@glog//:glog",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
    visibility = ["//visibility:public"],
)
