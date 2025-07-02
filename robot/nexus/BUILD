load("@rules_cc//cc:defs.bzl", "cc_library")
load("@pybind11_bazel//:build_defs.bzl", "pybind_library")

cc_library(
    name = "thread_pool",
    hdrs = ["thread_pool.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "nexus_scheduler",
    hdrs = ["nexus_scheduler.h"],
    visibility = ["//visibility:public"],
)

pybind_library(
    name = "ai_executor",
    srcs = ["ai_executor.cc"],
    hdrs = ["ai_executor.h"],
    data = [
        "//ai:ai_layer_gateway",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/nexus/proto:nexus_packet_cc_proto",
        "@com_github_google_glog//:glog",
    ],
)

cc_library(
    name = "interface_variant",
    hdrs = ["interface_variant.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/actuation/interfaces:actuation_interface",
        "//robot/perception/interfaces:perception_interface",
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
        ":ai_executor",
        "//robot/nexus/proto:nexus_packet_cc_proto",
        ":nexus_scheduler",
        ":interface_variant",
        ":thread_pool",
        "@com_github_google_glog//:glog",
    ],
)
