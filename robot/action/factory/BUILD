load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "action_factory",
    hdrs = ["action_factory.h"],
    deps = [
        "//robot/action/interfaces:action_interface",
        "//robot/action/motors/drivers:sts3215_driver",
        "//robot/comm_interface/factory:comm_factory",
        "//config/proto:robot_cc_proto",
        "@glog//:glog",
        "@abseil-cpp//absl/status:status",
    ],
    visibility = ["//visibility:public"],
)
