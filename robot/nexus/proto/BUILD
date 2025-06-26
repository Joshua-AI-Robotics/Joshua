load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "nexus_packet_proto",
    srcs = ["nexus_packet.proto"],
    deps = [
        "//robot/actuation/proto:actuation_packet_proto",
        "//robot/perception/proto:perception_packet_proto",
    ],
)

cc_proto_library(
    name = "nexus_packet_cc_proto",
    deps = [":nexus_packet_proto"],
) 