load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_library(
    name = "proto_parser",
    srcs = ["proto_parser.cc"],
    hdrs = ["proto_parser.h"],
    deps = [
        "//config/proto:config_cc_proto",
        "//robot/action/proto:action_cc_proto",
        "//robot/perception/proto:perception_cc_proto",
        "//robot/comm_interface/proto:comm_interface_cc_proto",
        "@protobuf//:protobuf",
    ],
    copts = [
        "-std=c++17",
    ],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "simple_fltk_gui",
    srcs = ["simple_fltk_gui.cc"],
    deps = [
        "@local_deps//:fltk",
    ],
    copts = [
        "-std=c++17",
    ],
)

cc_binary(
    name = "advanced_fltk_gui",
    srcs = ["advanced_fltk_gui.cc"],
    deps = [
        "@local_deps//:fltk",
        "//config/proto:config_cc_proto",
        "//robot/action/proto:action_cc_proto",
        "//robot/perception/proto:perception_cc_proto",
        "//robot/comm_interface/proto:comm_interface_cc_proto",
        "@protobuf//:protobuf",
    ],
    copts = [
        "-std=c++17",
    ],
)

cc_binary(
    name = "dynamic_fltk_gui",
    srcs = ["dynamic_fltk_gui.cc"],
    deps = [
        "@local_deps//:fltk",
        "//config/proto:config_cc_proto",
        "//robot/action/proto:action_cc_proto",
        "//robot/perception/proto:perception_cc_proto",
        "//robot/comm_interface/proto:comm_interface_cc_proto",
        "@protobuf//:protobuf",
        ":proto_parser",
    ],
    copts = [
        "-std=c++17",
    ],
)

cc_binary(
    name = "test_proto_parser",
    srcs = ["test_proto_parser.cc"],
    deps = [
        "//config/proto:config_cc_proto",
        "//robot/action/proto:action_cc_proto",
        "//robot/perception/proto:perception_cc_proto",
        "//robot/comm_interface/proto:comm_interface_cc_proto",
        "@protobuf//:protobuf",
        ":proto_parser",
    ],
    copts = [
        "-std=c++17",
    ],
)
