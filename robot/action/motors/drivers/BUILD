load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_library")

cc_library(
    name = "sts3215_driver",
    srcs = ["sts3215_driver.cc"],
    hdrs = ["sts3215_driver.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/action/interfaces:actuator_interface",
        "//robot/comm/serial",
        "//utils:status_macros",
    ],
)

py_library(
    name = "mock_driver_py",
    srcs = ["mock_driver.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/action/interfaces:actuator_interface_py",
        "//robot/action/proto:action_packet_py_proto",
        "//robot/action/proto:action_py_proto",
    ],
)
