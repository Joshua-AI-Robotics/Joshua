load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "sts3215_driver",
    srcs = ["sts3215_driver.cc"],
    hdrs = ["sts3215_driver.h"],
    deps = [
        "//robot/action/interfaces:actuator_interface",
        "//robot/comm_interface/serial:serial",
        "//config/proto:robot_cc_proto",
    ],
    visibility = ["//visibility:public"],
)
