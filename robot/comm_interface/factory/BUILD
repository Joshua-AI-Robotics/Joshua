load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "comm_factory",
    srcs = ["comm_factory.cc"],
    hdrs = ["comm_factory.h"],
    deps = [
        "//robot/comm_interface/serial",
        "//config/proto:robot_cc_proto",
        "@glog//:glog",
        "@boost.asio",
    ],
    visibility = ["//visibility:public"],
) 