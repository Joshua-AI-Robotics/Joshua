load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_binary(
    name = "proto_gui",
    srcs = ["proto_gui.cc"],
    deps = [
        "@local_deps//:fltk",
        "//config/proto:config_cc_proto",
        "//config:config_utils",
    ],
    copts = [
        "-std=c++17",
    ],
)
