load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "comm_factory",
    srcs = ["comm_factory.cc"],
    hdrs = ["comm_factory.h"],
    deps = [
        "//robot/comm_interface/serial:serial",
        "//robot/config:robot_cc_proto",
        "@com_github_google_glog//:glog",
        "@local_deps//:boost_asio",
    ],
    visibility = ["//visibility:public"],
) 