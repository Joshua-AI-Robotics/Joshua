load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_binary(
    name = "proto_gui_main",
    srcs = ["proto_gui_main.cc"],
    deps = [
        ":proto_gui",
    ],
)

cc_library(
    name = "proto_gui",
    hdrs = ["proto_gui.h"],
    deps = [
        "@local_deps//:fltk",
        "//config/proto:config_cc_proto",
        "//config:config_utils",
        ":proto_gui_callbacks",
    ],
    copts = [
        "-std=c++17",
    ],
)

cc_library(
    name = "proto_gui_callbacks",
    hdrs = ["proto_gui_callbacks.h"],
    deps = [
        "@local_deps//:fltk",
    ],
)
