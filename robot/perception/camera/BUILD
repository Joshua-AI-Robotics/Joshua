load("//:requirements.bzl", "requirement")
load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_library")

cc_library(
    name = "cv_camera",
    srcs = ["cv_camera.cc"],
    hdrs = ["cv_camera.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/perception/interfaces:camera_interface",
        "//robot/perception/proto:perception_packet_cc_proto",
        "@glog",
        "@local_deps//:core",
        "@local_deps//:imgcodecs",
        "@local_deps//:videoio",
    ],
)

py_library(
    name = "mock_camera_py",
    srcs = ["mock_camera.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/perception/interfaces:camera_interface_py",
        "//robot/perception/proto:perception_packet_py_proto",
        "//robot/perception/proto:perception_py_proto",
    ] + requirement("glog"),
)
