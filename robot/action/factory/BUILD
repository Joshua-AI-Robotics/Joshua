load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "action_factory",
    hdrs = ["action_factory.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/action/interfaces:action_interface",
        "//robot/action/motors/drivers:sts3215_driver",
        "//robot/comm/factory:comm_factory",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)
