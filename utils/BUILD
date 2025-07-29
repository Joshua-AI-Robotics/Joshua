load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "xbox_controller",
    srcs = ["xbox_controller/xbox_controller.cc"],
    hdrs = ["xbox_controller/xbox_controller.h"],
    deps = [
        "@local_deps//:libevdev",
        "@glog//:glog",
    ],
    visibility = ["//visibility:public"],
)
