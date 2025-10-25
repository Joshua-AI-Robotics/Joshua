load("@rules_cc//cc:defs.bzl", "cc_library")

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
