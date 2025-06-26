load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")

proto_library(
    name = "nexus_packet_proto",
    srcs = ["proto/nexus_packet.proto"],
    deps = [
        "//robot/actuation/proto:actuation_packet_proto",
        "//robot/perception/proto:perception_packet_proto",
    ],
)

cc_proto_library(
    name = "nexus_packet",
    visibility = ["//visibility:public"],
    deps = [":nexus_packet_proto"],
)

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
        ":nexus_packet",
        ":nexus_scheduler",
        ":interface_variant",
        "@com_github_google_glog//:glog",
    ],
)
