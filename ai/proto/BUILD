load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "ai_model_proto",
    srcs = ["ai_model.proto"],
    deps = [
        "//ros2/proto:node_proto",
    ],
)

py_proto_library(
    name = "ai_model_py_proto",
    visibility = ["//visibility:public"],
    deps = [":ai_model_proto"],
)
