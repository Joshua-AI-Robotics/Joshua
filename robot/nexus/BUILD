load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "nexus_scheduler",
    hdrs = ["nexus_scheduler.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "interface_variant",
    hdrs = ["interface_variant.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/actuation/interfaces:motor_interface",
        "//robot/perception/interfaces:camera_interface",
    ]
)

cc_library(
    name = "nexus",
    srcs = ["nexus.cc"],
    hdrs = [
        "nexus.h",
        "nexus_packet.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/nexus/proto:nexus_packet_cc_proto",
        ":nexus_scheduler",
        ":interface_variant",
        "@com_github_google_glog//:glog",
    ],
)
