load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "simulation_proto",
    srcs = ["simulation.proto"],
)

py_proto_library(
    name = "simulation_py_proto",
    deps = [":simulation_proto"],
)
