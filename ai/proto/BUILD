load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "ai_model_proto",
    srcs = ["ai_model.proto"],
    deps = [
        "//ros2/proto:node_proto",
        "//ros2/proto:ros2_data_type_proto",
    ],
)

proto_library(
    name = "data_store_proto",
    srcs = ["data_store.proto"],
    deps = [
        "//ros2/proto:ros2_data_type_proto",
    ],
)

py_proto_library(
    name = "ai_model_py_proto",
    visibility = ["//visibility:public"],
    deps = [":ai_model_proto"],
)

py_proto_library(
    name = "data_store_py_proto",
    visibility = ["//visibility:public"],
    deps = [":data_store_proto"],
)
