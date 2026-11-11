load("@pybind11_bazel//:build_defs.bzl", "pybind_extension")
load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("@rules_python//python:defs.bzl", "py_library", "py_test")

pybind_extension(
    name = "_robot_time",
    srcs = ["robot_time_pybind.cc"],
    deps = [
        ":robot_time",
        "//config/proto:robot_clock_cc_proto",
    ],
)

py_library(
    name = "robot_time_py",
    srcs = ["robot_time.py"],
    data = [":_robot_time"],
    visibility = ["//visibility:public"],
)

py_test(
    name = "robot_time_py_test",
    srcs = ["robot_time_py_test.py"],
    deps = [
        ":robot_time_py",
        "//config/proto:config_py_proto",
    ],
)

cc_library(
    name = "scope_exit",
    hdrs = ["scope_exit.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "joshua_runtime",
    srcs = ["joshua_runtime.cc"],
    hdrs = ["joshua_runtime.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":robot_time",
        "//config/proto:config_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@glog",
    ],
)

cc_test(
    name = "joshua_runtime_test",
    srcs = ["joshua_runtime_test.cc"],
    deps = [
        ":joshua_runtime",
        ":robot_time",
        ":scope_exit",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "robot_time",
    srcs = [
        "robot_time.cc",
        "robot_time_internal.h",
    ],
    hdrs = ["robot_time.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:robot_clock_cc_proto",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@glog",
    ],
)

cc_test(
    name = "robot_time_test",
    srcs = [
        "robot_time_internal.h",
        "robot_time_test.cc",
        "robot_time_test_helper.cc",
    ],
    deps = [
        ":robot_time",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "xbox_controller",
    srcs = ["xbox_controller/xbox_controller.cc"],
    hdrs = ["xbox_controller/xbox_controller.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@glog",
        "@local_deps//:libevdev",
    ],
)

cc_library(
    name = "status_macros",
    hdrs = ["status_macros.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)
