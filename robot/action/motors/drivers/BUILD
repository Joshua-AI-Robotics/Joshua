load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@rules_python//python:defs.bzl", "py_library", "py_test")

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

cc_library(
    name = "am243_ethercat_driver",
    srcs = ["am243_ethercat_driver.cc"],
    hdrs = ["am243_ethercat_driver.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/action/interfaces:actuator_interface",
        "//robot/comm/ethercat:ethercat_status",
        "//robot/comm/ethercat:ethercat_transport",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_library(
    name = "am243_pdo_codec",
    srcs = ["am243_pdo_codec.cc"],
    hdrs = ["am243_pdo_codec.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "am243_ethercat_driver_test",
    srcs = ["am243_ethercat_driver_test.cc"],
    deps = [
        ":am243_ethercat_driver",
        "//config/proto:robot_cc_proto",
        "//robot/action/proto:action_packet_cc_proto",
        "//robot/comm/ethercat:fake_ethercat_transport",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "am243_pdo_codec_test",
    srcs = ["am243_pdo_codec_test.cc"],
    deps = [
        ":am243_pdo_codec",
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
