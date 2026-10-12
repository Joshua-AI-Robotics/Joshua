load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_python//python:defs.bzl", "py_library", "py_test")

cc_library(
    name = "config_utils",
    hdrs = ["config_utils.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:config_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_library(
    name = "validation",
    srcs = ["validation.cc"],
    hdrs = ["validation.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":ros2_network",
        "//config/proto:config_cc_proto",
        "//robot/board/factory:board_resolver",
        "//ros2/utils:packet_parser",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "validation_test",
    srcs = ["validation_test.cc"],
    deps = [
        ":validation",
        "//config/proto:config_cc_proto",
        "//ros2/utils:packet_parser",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)

cc_library(
    name = "ros2_network",
    srcs = ["ros2_network.cc"],
    hdrs = ["ros2_network.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:config_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@glog",
    ],
)

cc_binary(
    name = "ros2_network_export",
    srcs = ["ros2_network_main.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":config_utils",
        ":ros2_network",
        "@gflags",
        "@glog",
        "@protobuf",
    ],
)

py_library(
    name = "ros2_network_py",
    srcs = ["ros2_network.py"],
    data = [":ros2_network_export"],
    visibility = ["//visibility:public"],
)

cc_test(
    name = "ros2_network_test",
    srcs = ["ros2_network_test.cc"],
    deps = [
        ":ros2_network",
        "@googletest//:gtest_main",
    ],
)

py_test(
    name = "ros2_network_py_test",
    srcs = ["ros2_network_test.py"],
    main = "ros2_network_test.py",
    deps = [
        ":ros2_network_py",
        "//config/proto:config_py_proto",
    ],
)
