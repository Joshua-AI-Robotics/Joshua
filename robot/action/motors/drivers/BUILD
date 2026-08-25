load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "sts3215_driver",
    srcs = ["sts3215_driver.cc"],
    hdrs = ["sts3215_driver.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/action/interfaces:actuator_interface",
        "//robot/board/interfaces:board_interfaces",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_test(
    name = "sts3215_driver_test",
    srcs = ["sts3215_driver_test.cc"],
    deps = [
        ":sts3215_driver",
        "//config/proto:robot_cc_proto",
        "//robot/action/proto:action_packet_cc_proto",
        "//robot/board/interfaces:board_interfaces",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "stepper_driver",
    srcs = ["stepper_driver.cc"],
    hdrs = ["stepper_driver.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/action/interfaces:actuator_interface",
        "//robot/board/interfaces:board_interfaces",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_test(
    name = "stepper_driver_test",
    srcs = ["stepper_driver_test.cc"],
    deps = [
        ":stepper_driver",
        "//config/proto:robot_cc_proto",
        "//robot/action/proto:action_packet_cc_proto",
        "//robot/board/interfaces:board_interfaces",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "ti_demo_driver",
    srcs = ["ti_demo_driver.cc"],
    hdrs = ["ti_demo_driver.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_cc_proto",
        "//robot/action/interfaces:actuator_interface",
        "//robot/board/interfaces:board_interfaces",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_test(
    name = "ti_demo_driver_test",
    srcs = ["ti_demo_driver_test.cc"],
    deps = [
        ":ti_demo_driver",
        "//config/proto:robot_cc_proto",
        "//robot/action/proto:action_packet_cc_proto",
        "//robot/board/interfaces:board_interfaces",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
