load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "perception_proto",
    srcs = ["perception.proto"],
    deps = [
        "//robot/comm_interface/proto:comm_interface_proto",
    ],
)

proto_library(
    name = "perception_packet_proto",
    srcs = ["perception_packet.proto"],
    deps = [":perception_proto"],
)

cc_proto_library(
    name = "perception_cc_proto",
    deps = [":perception_proto"],
)

cc_proto_library(
    name = "perception_packet_cc_proto",
    deps = [":perception_packet_proto"],
)