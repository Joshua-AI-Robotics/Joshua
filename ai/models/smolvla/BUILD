load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")
load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "smolvla_config_proto",
    srcs = ["smolvla_config.proto"],
)

py_proto_library(
    name = "smolvla_config_py_proto",
    visibility = ["//visibility:public"],
    deps = [":smolvla_config_proto"],
)

py_library(
    name = "smolvla",
    srcs = ["smolvla.py"],
    deps = [
        "//ai/models:model_base",
        ":smolvla_config_py_proto",
        requirement("numpy"),
        requirement("lerobot"),
    ],
)
