load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_library")
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "perception_interface",
    hdrs = ["perception_interface.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/status:status",
    ],
)

cc_library(
    name = "camera_interface",
    hdrs = ["camera_interface.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":perception_interface",
        "//config/proto:robot_cc_proto",
        "//robot/perception/proto:perception_packet_cc_proto",
    ],
)

cc_library(
    name = "encoder_interface",
    hdrs = ["encoder_interface.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":perception_interface",
        "//robot/perception/proto:perception_packet_cc_proto",
    ],
)

cc_library(
    name = "lidar_interface",
    hdrs = ["lidar_interface.h"],
    deps = [
        ":perception_interface",
        "//robot/perception/proto:perception_packet_cc_proto",
    ],
)

py_library(
    name = "perception_interface_py",
    srcs = ["perception_interface.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/perception/proto:perception_packet_py_proto",
    ],
)

py_library(
    name = "camera_interface_py",
    srcs = ["camera_interface.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":perception_interface_py",
    ],
)

py_library(
    name = "encoder_interface_py",
    srcs = ["encoder_interface.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":perception_interface_py",
    ],
)

py_library(
    name = "lidar_interface_py",
    srcs = ["lidar_interface.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":perception_interface_py",
    ],
)
