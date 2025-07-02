load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "actuation_interface",
    hdrs = ["actuation_interface.h"],
    deps = [
        "//robot/nexus/proto:nexus_packet_cc_proto",
        "@glog//:glog",
    ],
    visibility = ["//visibility:public"],
)
