load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "motor_interface",
    hdrs = ["motor_interface.h"],
    deps = [
        "//robot/nexus:nexus_packet",
        "@com_github_google_glog//:glog",
    ],
    visibility = ["//visibility:public"],
)
