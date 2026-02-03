load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "comm_proto",
    srcs = ["comm.proto"],
)

cc_proto_library(
    name = "comm_cc_proto",
    deps = [":comm_proto"],
)

py_proto_library(
    name = "comm_py_proto",
    deps = [":comm_proto"],
)
