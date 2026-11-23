load("@rules_python//python:defs.bzl", "py_library")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

filegroup(
    name = "sources",
    srcs = glob(["*.py"]) + [
        "//ai/models/decisions:adapter",
        "//ai/models/random_noise:adapter",
        "//ai/models/smolvla:adapter",
    ],
)

filegroup(
    name = "manifests",
    srcs = [
        "//ai/models/decisions:model.textproto",
        "//ai/models/random_noise:model.textproto",
        "//ai/models/smolvla:model.textproto",
    ],
)

filegroup(
    name = "locks",
    srcs = [
        "//ai/models/decisions:requirements.lock",
        "//ai/models/decisions:requirements_3.12.lock",
        "//ai/models/smolvla:requirements.lock",
        "//ai/models/smolvla:requirements_3.12.lock",
    ],
)

# Manifest-driven registry: no BUILD-time deps on individual model adapters.
py_library(
    name = "registry",
    srcs = ["registry.py"],
    deps = [
        "//ai/inference:adapter",
        "//ai/inference:manifest",
        "//ai/proto:ai_model_py_proto",
        "//config/proto:config_py_proto",
    ] + requirement("protobuf"),
)
