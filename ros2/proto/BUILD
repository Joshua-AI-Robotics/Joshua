load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "ros2_data_type_proto",
    srcs = ["ros2_data_type.proto"],
)

py_proto_library(
    name = "ros2_data_type_py_proto",
    visibility = ["//visibility:public"],
    deps = [":ros2_data_type_proto"],
)
