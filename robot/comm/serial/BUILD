load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "serial",
    srcs = ["serial.cc"],
    hdrs = ["serial.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@boost.asio",
        "@glog",
    ],
)
