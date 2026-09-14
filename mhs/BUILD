load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")
load("@rules_python//python:defs.bzl", "py_test")

cc_library(
    name = "runtime",
    srcs = ["runtime.cc"],
    hdrs = ["runtime.h"],
    deps = [
        "//config/proto:config_cc_proto",
        "//config/proto:hardware_api_cc_proto",
        "//robot/action/proto:action_cc_proto",
        "//robot/board/factory:board_resolver",
        "//robot/board/interfaces:board_interfaces",
        "//robot/board/proto:board_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@protobuf",
    ],
)

cc_binary(
    name = "executor",
    srcs = ["executor.cc"],
    deps = [
        ":runtime",
        "//config:config_utils",
        "//robot/board/teensy:teensy_board",
        "@glog",
        "@protobuf",
    ],
)

cc_test(
    name = "runtime_test",
    srcs = ["runtime_test.cc"],
    deps = [
        ":runtime",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)

py_test(
    name = "executor_test",
    srcs = [
        "client.py",
        "executor_test.py",
    ],
    args = [
        "$(location :executor)",
        "$(location //config/config_preset:example/teensy_hardware_api.pbtxt)",
    ],
    data = [
        ":executor",
        "//config/config_preset:example/teensy_hardware_api.pbtxt",
    ],
    imports = ["."],
)
