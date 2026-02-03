load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_library")

cc_library(
    name = "action_interface",
    hdrs = ["action_interface.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/action/proto:action_packet_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_library(
    name = "actuator_interface",
    hdrs = ["actuator_interface.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":action_interface",
        "@glog",
    ],
)

py_library(
    name = "action_interface_py",
    srcs = ["action_interface.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/action/proto:action_packet_py_proto",
    ],
)

py_library(
    name = "actuator_interface_py",
    srcs = ["actuator_interface.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":action_interface_py",
    ],
)
