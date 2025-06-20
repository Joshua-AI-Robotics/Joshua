load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "motor_factory",
    hdrs = ["motor_factory.h"],
    deps = [
        "//robot/onboard/interfaces:motor_interface",
        "//robot/onboard/drivers:sts3215_driver",
        "//robot/comm_interface/serial:serial",
    ],
    visibility = ["//visibility:public"],
)
