load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "board_proto",
    srcs = ["board.proto"],
    deps = [
        "//robot/comm/proto:comm_proto",
    ],
)

cc_proto_library(
    name = "board_cc_proto",
    deps = [
        ":board_proto",
    ],
)

py_proto_library(
    name = "board_py_proto",
    deps = [
        ":board_proto",
    ],
)
