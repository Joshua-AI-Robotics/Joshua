load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "action_interface",
    hdrs = ["action_interface.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/action/proto:action_packet_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_library(
    name = "actuator_interface",
    hdrs = ["actuator_interface.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":action_interface",
        "//utils:robot_time",
        "@abseil-cpp//absl/status:statusor",
        "@glog",
    ],
)

cc_library(
    name = "control_gate",
    hdrs = ["control_gate.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":action_interface",
        "//robot/action/proto:action_cc_proto",
        "//utils:robot_time",
    ],
)

cc_test(
    name = "control_gate_test",
    srcs = ["control_gate_test.cc"],
    deps = [
        ":control_gate",
        "@googletest//:gtest_main",
    ],
)
