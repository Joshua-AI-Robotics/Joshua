load("@rules_python//python:defs.bzl", "py_library")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "sources",
    srcs = glob(["*.py"]) + [
        "//ai/models/random_noise:adapter",
        "//ai/models/smolvla:adapter",
    ],
)

filegroup(
    name = "manifests",
    srcs = [
        "//ai/models/random_noise:model.textproto",
        "//ai/models/smolvla:model.textproto",
    ],
)

# Manifest-driven registry: no BUILD-time deps on individual model adapters.
py_library(
    name = "registry",
    srcs = ["registry.py"],
    deps = [
        "//ai/proto:ai_model_py_proto",
        "//ai/inference:adapter",
        "//ai/inference:manifest",
        "//config/proto:config_py_proto",
    ] + requirement("protobuf"),
)
