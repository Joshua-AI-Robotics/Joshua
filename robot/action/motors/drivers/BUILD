load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_python//python:defs.bzl", "py_library", "py_test")

cc_library(
    name = "sts3215_driver",
    srcs = ["sts3215_driver.cc"],
    hdrs = ["sts3215_driver.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/action/interfaces:actuator_interface",
        "//robot/board/interfaces:board_interfaces",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_test(
    name = "sts3215_driver_test",
    srcs = ["sts3215_driver_test.cc"],
    deps = [
        ":sts3215_driver",
        "//config/proto:robot_cc_proto",
        "//robot/action/proto:action_packet_cc_proto",
        "//robot/board/interfaces:board_interfaces",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "ti_demo_driver",
    srcs = ["ti_demo_driver.cc"],
    hdrs = ["ti_demo_driver.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/action/interfaces:actuator_interface",
        "//robot/board/interfaces:board_interfaces",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_test(
    name = "ti_demo_driver_test",
    srcs = ["ti_demo_driver_test.cc"],
    deps = [
        ":ti_demo_driver",
        "//config/proto:robot_cc_proto",
        "//robot/action/proto:action_packet_cc_proto",
        "//robot/board/interfaces:board_interfaces",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
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

py_library(
    name = "pybricks_driver_py",
    srcs = ["pybricks_driver.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/action/interfaces:actuator_interface_py",
        "//robot/action/proto:action_packet_py_proto",
        "//robot/action/proto:action_py_proto",
        "//robot/comm:pybricks_ble_transport_py",
        "//robot/comm/proto:comm_py_proto",
    ],
)

py_test(
    name = "pybricks_driver_test",
    srcs = ["pybricks_driver_test.py"],
    deps = [
        ":pybricks_driver_py",
        "//robot/action/proto:action_packet_py_proto",
        "//robot/action/proto:action_py_proto",
        "//robot/comm/proto:comm_py_proto",
    ],
)
