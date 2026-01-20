load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_library")

cc_library(
    name = "comm_factory",
    srcs = ["comm_factory.cc"],
    hdrs = ["comm_factory.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/comm/serial",
        "@abseil-cpp//absl/status:status",
        "@boost.asio",
        "@glog",
    ],
)

py_library(
    name = "comm_factory_py",
    srcs = ["comm_factory.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/comm/proto:comm_py_proto",
        "//robot/comm/serial:serial_py",
    ],
)
