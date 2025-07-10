load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "actuation_factory",
    hdrs = ["actuation_factory.h"],
    deps = [
        "//robot/actuation/interfaces:actuation_interface",
        "//robot/actuation/motors/drivers:sts3215_driver",
        "//robot/comm_interface/factory:comm_factory",
        "//configs/proto:robot_cc_proto",
    ],
    visibility = ["//visibility:public"],
)
