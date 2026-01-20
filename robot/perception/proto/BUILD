load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "perception_proto",
    srcs = ["perception.proto"],
    deps = [
        "//robot/comm/proto:comm_proto",
        "//ros2/proto:node_proto",
    ],
)

proto_library(
    name = "perception_packet_proto",
    srcs = ["perception_packet.proto"],
)

cc_proto_library(
    name = "perception_cc_proto",
    deps = [":perception_proto"],
)

cc_proto_library(
    name = "perception_packet_cc_proto",
    deps = [":perception_packet_proto"],
)

py_proto_library(
    name = "perception_py_proto",
    deps = [":perception_proto"],
)

py_proto_library(
    name = "perception_packet_py_proto",
    deps = [":perception_packet_proto"],
)
