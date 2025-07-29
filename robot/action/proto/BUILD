load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "action_proto",
    srcs = ["action.proto"],
    deps = [
        "//robot/comm_interface/proto:comm_interface_proto",
    ],
)

cc_proto_library(
    name = "action_cc_proto",
    deps = [
        ":action_proto",
    ],
)

proto_library(
    name = "action_packet_proto",
    srcs = ["action_packet.proto"],
    deps = [
        ":action_proto",
    ],
)

cc_proto_library(
    name = "action_packet_cc_proto",
    deps = [":action_packet_proto"],
)
