load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "camera_interface",
    hdrs = ["camera_interface.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/nexus:nexus_packet",
        "@opencv//:core",
    ],
)
