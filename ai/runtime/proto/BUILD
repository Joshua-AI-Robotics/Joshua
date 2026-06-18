load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "runtime_proto",
    srcs = ["runtime.proto"],
)

proto_library(
    name = "model_manifest_proto",
    srcs = ["model_manifest.proto"],
    deps = ["//ai/proto:ai_model_proto"],
)

py_proto_library(
    name = "runtime_py_proto",
    visibility = ["//visibility:public"],
    deps = [":runtime_proto"],
)

py_proto_library(
    name = "model_manifest_py_proto",
    visibility = ["//visibility:public"],
    deps = [":model_manifest_proto"],
)
