load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")
load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

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
    name = "random_noise",
    srcs = ["random_noise.py"],
    deps = [
        "//ai/models:model_base",
        ":random_noise_config_py_proto",
    ] + requirement("numpy"),
)
