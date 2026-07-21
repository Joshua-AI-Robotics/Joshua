load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_library")

cc_library(
    name = "serial",
    srcs = ["serial.cc"],
    hdrs = ["serial.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@boost.asio",
        "@glog",
    ],
)

cc_library(
    name = "fake_serial_transport",
    testonly = True,
    hdrs = ["fake_serial_transport.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":serial",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

py_library(
    name = "serial_py",
    srcs = ["serial.py"],
    visibility = ["//visibility:public"],
)
