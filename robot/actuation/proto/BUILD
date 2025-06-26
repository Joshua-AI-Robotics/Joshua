load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")

proto_library(
    name = "actuation_proto",
    srcs = ["actuation.proto"],
    deps = [
        "//robot/comm_interface/proto:comm_interface_proto",
    ],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "actuation_cc_proto",
    deps = [
        ":actuation_proto",
    ],
    visibility = ["//visibility:public"],
)

proto_library(
    name = "actuation_packet_proto",
    srcs = ["actuation_packet.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "actuation_packet_cc_proto",
    deps = [":actuation_packet_proto"],
    visibility = ["//visibility:public"],
)
