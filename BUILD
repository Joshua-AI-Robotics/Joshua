load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "main_program",
    srcs = ["main.cc"],
    data = ["//robot/config:robot_config_pbtxt"],
    deps = [
        "//robot/actuation/factory:motor_factory",
    ],
    visibility = ["//visibility:public"],
)