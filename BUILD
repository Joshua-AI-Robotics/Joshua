load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "main_program",
    srcs = ["main.cc"],
    deps = [
        "//robot/onboard/factory:motor_factory",
    ],
    visibility = ["//visibility:public"],
)