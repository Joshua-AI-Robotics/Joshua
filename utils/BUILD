load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "so100_xbox_controller_handler",
    srcs = ["so100_xbox_controller_handler.cc"],
    hdrs = ["so100_xbox_controller_handler.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/actuation/interfaces:actuation_interface",
        "//robot/actuation/motors/drivers:sts3215_driver",
        "//robot/config:robot_cc_proto",
        "//robot/config:config_utils",
        "//utils/xbox_controller:xbox_controller",
        "@com_github_google_glog//:glog",
    ],
)

cc_binary(
    name = "so100_control",
    srcs = ["so100_control.cc"],
    data = ["//robot/config:robot_config_pbtxt"],
    deps = [
        "//robot/actuation/factory:actuation_factory",
        "//utils:so100_xbox_controller_handler",
    ],
    visibility = ["//visibility:public"],
)


cc_binary(
    name = "camera_main",
    srcs = ["camera_main.cc"],
    data = ["//robot/config:robot_config_pbtxt"],
    deps = [
        "//robot/config:config_utils",
        "//robot/perception/factory:camera_factory",
        "//robot/perception/interfaces:camera_interface",
        "//robot/nexus/proto:nexus_packet_cc_proto",
        "@com_github_google_glog//:glog",
        "@local_deps//:highgui",
        "@local_deps//:imgcodecs",
    ],
)
