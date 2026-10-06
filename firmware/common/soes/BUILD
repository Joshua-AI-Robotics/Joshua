load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "options",
    hdrs = [
        "cc.h",
        "ecat_options.h",
    ],
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "joshua_ethercat_soes",
    srcs = ["joshua_ethercat_soes.c"],
    hdrs = ["joshua_ethercat_soes.h"],
    copts = [
        "-x",
        "c",
        "-std=c11",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//firmware/common:joshua_ethercat_profile",
        "@soes",
    ],
)
