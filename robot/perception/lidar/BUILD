load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_library")
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "lds01_driver",
    hdrs = ["lds01_driver.h"],
    srcs = ["lds01_driver.cc"],
    deps = [
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "//robot/comm/serial:serial",
        "//config/proto:robot_cc_proto",
        "//robot/perception/interfaces:lidar_interface",
        "//robot/perception/proto:perception_packet_cc_proto",
        "@glog//:glog",
        "@boost.asio",
    ],
)

py_library(
    name = "mock_lidar_py",
    srcs = ["mock_lidar.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/perception/interfaces:lidar_interface_py",
        "//robot/perception/proto:perception_packet_py_proto",
        "//robot/perception/proto:perception_py_proto",
    ],
)
