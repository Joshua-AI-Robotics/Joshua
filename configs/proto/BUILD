load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")

proto_library(
    name = "config_proto",
    srcs = ["config.proto"],
    deps = [
        "//robot/actuation/proto:actuation_proto",
        "//robot/perception/proto:perception_proto",
    ],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "config_cc_proto",
    deps = [":config_proto"],
    visibility = ["//visibility:public"],
)

