load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "runtime_proto",
    srcs = ["runtime.proto"],
)

py_proto_library(
    name = "runtime_py_proto",
    visibility = ["//visibility:public"],
    deps = [":runtime_proto"],
)
