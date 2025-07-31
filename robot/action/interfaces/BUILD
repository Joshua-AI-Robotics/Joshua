load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "action_interface",
    hdrs = ["action_interface.h"],
    deps = [
        "@glog//:glog",
        "//robot/action/proto:action_packet_cc_proto",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "actuator_interface",
    hdrs = ["actuator_interface.h"],
    deps = [
        "@glog//:glog",
        ":action_interface",
    ],
    visibility = ["//visibility:public"],
)