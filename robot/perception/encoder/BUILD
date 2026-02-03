load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_library")

cc_library(
    name = "sts3215_encoder",
    srcs = ["sts3215_encoder.cc"],
    hdrs = ["sts3215_encoder.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/comm/serial",
        "//robot/perception/interfaces:encoder_interface",
        "//robot/perception/proto:perception_cc_proto",
        "//robot/perception/proto:perception_packet_cc_proto",
        "//utils:status_macros",
        "@glog",
    ],
)

py_library(
    name = "mock_encoder_py",
    srcs = ["mock_encoder.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/perception/interfaces:encoder_interface_py",
        "//robot/perception/proto:perception_packet_py_proto",
        "//robot/perception/proto:perception_py_proto",
    ],
)
