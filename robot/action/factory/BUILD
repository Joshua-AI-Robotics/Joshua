load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "action_factory",
    hdrs = ["action_factory.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/action/interfaces:action_interface",
        "//robot/action/motors/drivers:stepper_driver",
        "//robot/action/motors/drivers:sts3215_driver",
        "//robot/action/motors/drivers:ti_demo_driver",
        "//robot/board/factory:board_factory",
        "//robot/board/factory:board_resolver",
        "//robot/board/factory:motor_channel_validation",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_test(
    name = "action_factory_cc_test",
    srcs = ["action_factory_test.cc"],
    deps = [
        ":action_factory",
        "//config/proto:robot_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
