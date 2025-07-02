load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "sts3215_encoder",
    srcs = ["sts3215_encoder.cc"],
    hdrs = ["sts3215_encoder.h"],
    deps = [
        "//robot/perception/interfaces:encoder_interface",
        "//robot/perception/proto:perception_cc_proto",
        "//robot/comm_interface/serial:serial",
        "@glog//:glog",
    ],
    visibility = ["//visibility:public"],
) 