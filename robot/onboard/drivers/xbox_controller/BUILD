load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "xbox_controller",
    srcs = ["xbox_controller.cc"],
    hdrs = ["xbox_controller.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/onboard/interfaces:motor_interface",
        "@libevdev//:libevdev",
        "@com_github_google_glog//:glog",
    ],
) 