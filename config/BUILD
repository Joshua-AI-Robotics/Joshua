load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_python//python:defs.bzl", "py_test")

cc_library(
    name = "config_utils",
    hdrs = ["config_utils.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:config_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/strings",
        "@glog",
        "@protobuf",
    ],
)

cc_library(
    name = "validation",
    srcs = ["validation.cc"],
    hdrs = ["validation.h"],
    visibility = ["//visibility:public"],
    deps = [
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
    name = "config_validation_cli",
    srcs = ["config_validation_cli.cc"],
    hdrs = ["config_validation_cli.h"],
    deps = [
        ":config_utils",
        ":validation",
        "@abseil-cpp//absl/status:status",
        "@protobuf",
    ],
)

cc_binary(
    name = "joshua_validate_config",
    srcs = ["validation_main.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":config_validation_cli",
        "@glog",
    ],
)

cc_test(
    name = "config_validation_cli_test",
    srcs = ["config_validation_cli_test.cc"],
    deps = [
        ":config_utils",
        ":config_validation_cli",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)

genquery(
    name = "validation_dependency_graph",
    expression = "deps(//config:joshua_validate_config)",
    scope = ["//config:joshua_validate_config"],
)

py_test(
    name = "validation_cli_test",
    srcs = ["validation_cli_test.py"],
    data = [
        ":joshua_validate_config",
        ":validation_dependency_graph",
    ],
)
