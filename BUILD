load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "main_program",
    srcs = ["main.cc"],
    deps = [
        "//robot/onboard/factory:motor_factory",
        "//robot/onboard/drivers/xbox_controller:xbox_controller",
    ],
    visibility = ["//visibility:public"],
)