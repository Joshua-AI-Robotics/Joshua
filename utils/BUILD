load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_binary(
    name = "camera_main",
    srcs = ["camera_main.cc"],
    data = ["//config/config_preset:so100_with_example_ai"],
    deps = [
        "//robot/perception/camera:cv_camera",
        "//config:config_utils",
        "//config/proto:config_cc_proto",
        "@glog//:glog",
        "@local_deps//:core",
        "@local_deps//:highgui",
        "//robot/perception/factory:perception_factory",
        "//robot/perception/interfaces:perception_interface",
        "//robot/nexus/proto:nexus_packet_cc_proto",
    ],
)

cc_library(
    name = "xbox_controller",
    srcs = ["xbox_controller/xbox_controller.cc"],
    hdrs = ["xbox_controller/xbox_controller.h"],
    deps = [
        "@local_deps//:libevdev",
        "@glog//:glog",
    ],
    visibility = ["//visibility:public"],
)
