load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@rules_python//python:defs.bzl", "py_library", "py_test")

cc_library(
    name = "action_factory",
    hdrs = ["action_factory.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/action/interfaces:action_interface",
        "//robot/action/motors/drivers:am243_ethercat_driver",
        "//robot/action/motors/drivers:sts3215_driver",
        "//robot/comm/factory:comm_factory",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_test(
    name = "action_factory_cc_test",
    srcs = ["action_factory_test.cc"],
    deps = [
        ":action_factory",
        "//config/proto:robot_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)

py_library(
    name = "action_factory_py",
    srcs = ["action_factory.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/action/interfaces:action_interface_py",
        "//robot/action/motors/drivers:mock_driver_py",
        "//robot/action/motors/drivers:pybricks_driver_py",
        "//robot/action/proto:action_py_proto",
    ],
)

py_test(
    name = "action_factory_test",
    srcs = ["action_factory_test.py"],
    deps = [
        ":action_factory_py",
        "//robot/action/motors/drivers:mock_driver_py",
        "//robot/action/proto:action_py_proto",
        "//robot/comm/proto:comm_py_proto",
    ],
)
