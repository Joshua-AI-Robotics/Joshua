load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:defs.bzl", "py_library", "py_test")
load("//:requirements.bzl", "requirement")
load("//ai/models:requirements.bzl", "model_requirement")

package(default_visibility = ["//visibility:public"])

exports_files([
    "model.textproto",
    "requirements.in",
    "requirements.lock",
    "requirements_3.12.lock",
])

proto_library(
    name = "decisions_config_proto",
    srcs = ["decisions_config.proto"],
    deps = ["//ai/inference/proto:motor_policy_proto"],
)

py_library(
    name = "adapter",
    srcs = ["adapter.py"],
    deps = [
        "//ai/inference:adapter",
        "//ai/inference:motor_policy",
        "//ai/inference:policy_session",
        "//ai/inference:types",
        "//ai/inference/proto:motor_policy_py_proto",
    ] + requirement("pillow") + model_requirement("decisions", "openai"),
)

py_test(
    name = "adapter_test",
    srcs = ["tests/adapter_test.py"],
    data = [
        "model.textproto",
        "//config:joshua_validate_config",
        "//config/config_preset:config_presets",
    ],
    main = "tests/adapter_test.py",
    deps = [
        ":adapter",
        "//ai/inference:motor_policy",
        "//ai/models:registry",
        "//config/proto:config_py_proto",
        "//robot/action/proto:action_packet_py_proto",
    ] + requirement("protobuf") + model_requirement("decisions", "httpx2") + model_requirement("decisions", "openai"),
)
