load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")

proto_library(
    name = "comm_interface_proto",
    srcs = ["comm_interface.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "comm_interface_cc_proto",
    deps = [":comm_interface_proto"],
    visibility = ["//visibility:public"],
)
