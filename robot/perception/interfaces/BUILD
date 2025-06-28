load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "camera_interface",
    hdrs = ["camera_interface.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/config:robot_cc_proto",
        "//robot/nexus/proto:nexus_packet_cc_proto",
        "@local_deps//:core",
        "@local_deps//:opencv",
    ],
)
