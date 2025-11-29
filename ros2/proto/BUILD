load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "node_proto",
    srcs = ["node.proto"],
    deps = [
        ":ros2_data_type_proto",
    ],
)

py_proto_library(
    name = "node_py_proto",
    visibility = ["//visibility:public"],
    deps = [":node_proto"],
)

cc_proto_library(
    name = "node_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":node_proto"],
)

proto_library(
    name = "ros2_data_type_proto",
    srcs = ["ros2_data_type.proto"],
)

py_proto_library(
    name = "ros2_data_type_py_proto",
    visibility = ["//visibility:public"],
    deps = [":ros2_data_type_proto"],
)
