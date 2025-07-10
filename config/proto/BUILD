load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

proto_library(
    name = "config_proto",
    srcs = ["config.proto"],
    deps = [
        ":robot_proto",
        ":ai_proto",
    ],
    visibility = ["//visibility:public"],
)

py_proto_library(
    name = "config_py_proto",
    deps = [":config_proto"],
    visibility = ["//visibility:public"],
)

proto_library(
    name = "robot_proto",
    srcs = ["robot.proto"],
    deps = [
        "//robot/actuation/proto:actuation_proto",
        "//robot/perception/proto:perception_proto",
    ],
    visibility = ["//visibility:public"],
)

proto_library(
    name = "ai_proto",
    srcs = ["ai.proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "config_cc_proto",
    deps = [":config_proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "robot_cc_proto",
    deps = [":robot_proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "ai_cc_proto",
    deps = [":ai_proto"],
    visibility = ["//visibility:public"],
)
