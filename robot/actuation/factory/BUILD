load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "actuation_factory",
    hdrs = ["actuation_factory.h"],
    deps = [
        "//robot/actuation/interfaces:actuation_interface",
        "//robot/actuation/motors/drivers:sts3215_driver",
        "//robot/comm_interface/serial:serial",
        "//robot/config:robot_cc_proto",
    ],
    visibility = ["//visibility:public"],
)
