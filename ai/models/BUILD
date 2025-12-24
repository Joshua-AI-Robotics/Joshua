load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "model_base",
    srcs = ["model_base.py"],
    deps = [
        "//ai/proto:ai_model_py_proto",
    ] + requirement("numpy") + requirement("protobuf"),
)

py_library(
    name = "model_registry",
    srcs = ["model_registry.py"],
    deps = [
        ":model_base",
        "//ai/proto:ai_model_py_proto",
        # Add all models here
        "//ai/models/random_noise:random_noise",
    ] + requirement("protobuf"),
)
