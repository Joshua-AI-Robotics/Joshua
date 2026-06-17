load("@rules_python//python:defs.bzl", "py_library")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

# Single wiring point: ModelType -> adapter. The engine (//ai/runtime)
# stays model-agnostic; each model lives in its own subpackage holding
# both its config proto and its adapter implementation.
py_library(
    name = "registry",
    srcs = ["registry.py"],
    deps = [
        "//ai/models/random_noise:adapter",
        "//ai/models/smolvla:adapter",
        "//ai/proto:ai_model_py_proto",
        "//ai/runtime:adapter",
        "//config/proto:config_py_proto",
    ] + requirement("protobuf"),
)
