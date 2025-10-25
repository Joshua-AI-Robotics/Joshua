load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "comm_factory",
    srcs = ["comm_factory.cc"],
    hdrs = ["comm_factory.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/comm/serial",
        "@abseil-cpp//absl/status:status",
        "@boost.asio",
        "@glog",
    ],
)
