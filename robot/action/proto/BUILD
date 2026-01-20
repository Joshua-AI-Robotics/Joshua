load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "action_proto",
    srcs = ["action.proto"],
    deps = [
        "//robot/comm/proto:comm_proto",
        "//robot/perception/proto:perception_proto",
        "//ros2/proto:node_proto",
    ],
)

proto_library(
    name = "action_packet_proto",
    srcs = ["action_packet.proto"],
)

cc_proto_library(
    name = "action_cc_proto",
    deps = [
        ":action_proto",
    ],
)

cc_proto_library(
    name = "action_packet_cc_proto",
    deps = [
        ":action_packet_proto",
    ],
)

py_proto_library(
    name = "action_py_proto",
    deps = [
        ":action_proto",
    ],
)

py_proto_library(
    name = "action_packet_py_proto",
    deps = [
        ":action_packet_proto",
    ],
)
