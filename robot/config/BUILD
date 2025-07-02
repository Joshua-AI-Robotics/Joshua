load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library", "cc_library")

proto_library(
    name = "robot_proto",
    srcs = ["robot.proto"],
    deps = [
        "//robot/actuation/proto:actuation_proto",
        "//robot/perception/proto:perception_proto",
    ],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "robot_cc_proto",
    deps = [":robot_proto"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "config_utils",
    hdrs = ["config_utils.h"],
    deps = [
        "@glog//:glog",
        ":robot_cc_proto",
    ],
    visibility = ["//visibility:public"],
)


filegroup(
    name = "robot_config_pbtxt",
    srcs = ["robot_config.pbtxt"],
    visibility = ["//visibility:public"],
)