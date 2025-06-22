load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "cv_camera",
    srcs = ["cv_camera.cc"],
    hdrs = ["cv_camera.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/perception/interfaces:camera_interface",
        "@com_github_google_glog//:glog",
        "//robot/config:robot_cc_proto",
        "@opencv//:videoio",
        "@opencv//:core",
    ],
)
