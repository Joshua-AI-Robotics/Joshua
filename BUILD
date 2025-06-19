load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "main_program",
    srcs = ["main.cc"],
    deps = [
        "//robot/comm_interface/serial:serial",
    ],
    visibility = ["//visibility:public"],
)