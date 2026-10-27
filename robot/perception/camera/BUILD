load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

exports_files(["cv_camera.cc"])

cc_library(
    name = "cv_camera",
    srcs = ["cv_camera.cc"],
    hdrs = ["cv_camera.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/perception/interfaces:camera_interface",
        "//robot/perception/proto:perception_cc_proto",
        "//robot/perception/proto:perception_packet_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@glog",
        "@local_deps//:core",
        "@local_deps//:imgcodecs",
        "@local_deps//:videoio",
    ],
)

cc_test(
    name = "cv_camera_test",
    srcs = ["cv_camera_test.cc"],
    deps = [
        ":cv_camera",
        "@googletest//:gtest_main",
    ],
)
