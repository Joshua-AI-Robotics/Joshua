load("@project_joshua_pip_deps//:requirements.bzl", "requirement")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:defs.bzl", "py_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

exports_files(["model.textproto"])

proto_library(
    name = "random_noise_config_proto",
    srcs = ["random_noise_config.proto"],
)

py_proto_library(
    name = "random_noise_config_py_proto",
    visibility = ["//visibility:public"],
    deps = [":random_noise_config_proto"],
)

py_library(
    name = "adapter",
    srcs = ["adapter.py"],
    data = ["model.textproto"],
    deps = [
        "//ai/proto:ai_model_py_proto",
        "//ai/inference:adapter",
        "//ai/inference:types",
        "//config/proto:config_py_proto",
        requirement("protobuf"),
    ],
)
