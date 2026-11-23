load("//:requirements.bzl", "requirement")
load(":requirements.bzl", "mcp_requirement")
load("//ai/models:requirements.bzl", "model_requirement")
load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")

exports_files([
    "requirements.in",
    "requirements.lock",
    "requirements_3.12.lock",
])

py_library(
    name = "config_validation_lib",
    srcs = [
        "config_validation_server.py",
        "hardware_catalog.py",
        "inspection_adapters.py",
        "inspection_server.py",
        "interfaces.py",
        "snapshot_observer.py",
    ],
    data = ["//docs/hardware_catalog:records"],
    deps = ["//tools:hardware_catalog_lib"] + mcp_requirement("mcp"),
)

py_binary(
    name = "config_validation_server",
    srcs = ["main.py"],
    main = "main.py",
    deps = [":config_validation_lib"],
)

py_test(
    name = "hardware_catalog_test",
    srcs = ["hardware_catalog_test.py"],
    data = [
        "//config/config_preset:config_presets",
        "//docs:SUPPORTED_HARDWARE.md",
        "//docs:TESTED_HARDWARE.md",
        "//docs/hardware_catalog:records",
        "//robot/perception/camera:cv_camera.cc",
    ],
    deps = [
        ":config_validation_lib",
        "//tools:hardware_catalog_lib",
    ],
)

py_test(
    name = "config_validation_server_test",
    srcs = ["config_validation_server_test.py"],
    data = [
        ":config_validation_server",
        "//config:joshua_validate_config",
    ],
    deps = [":config_validation_lib"] + mcp_requirement("mcp"),
)

py_test(
    name = "inspection_server_test",
    size = "medium",
    timeout = "moderate",
    srcs = ["inspection_server_test.py"],
    data = [
        ":config_validation_server",
        "//config:joshua_inspect_config",
        "//config:joshua_validate_config",
        "//launcher/node_manager:observation_fixture",
    ],
    tags = ["local"],
    deps = [":config_validation_lib"] + mcp_requirement("mcp"),
)

py_library(
    name = "observation_source",
    srcs = ["observation_source.py"],
)

py_test(
    name = "observation_source_test",
    srcs = ["observation_source_test.py"],
    deps = [":observation_source"],
)

py_binary(
    name = "setup_smoke",
    srcs = ["setup_smoke.py"],
    deps = mcp_requirement("mcp"),
)

py_test(
    name = "setup_smoke_test",
    size = "medium",
    srcs = ["setup_smoke.py"],
    args = ["--test"],
    data = [
        ":config_validation_server",
        "//config:joshua_inspect_config",
        "//config:joshua_validate_config",
    ],
    main = "setup_smoke.py",
    deps = mcp_requirement("mcp"),
)

py_library(
    name = "decisions_request",
    srcs = ["decisions_request.py"],
    deps = ["//ai/models/decisions:choice"] + model_requirement("decisions", "openai"),
)

py_binary(
    name = "decisions_server",
    srcs = ["decisions_server.py"],
    deps = [
        ":config_validation_lib",
        ":decisions_request",
        ":head_replay",
    ] + model_requirement("decisions", "openai") + mcp_requirement("mcp"),
)

py_binary(
    name = "decisions_replay_demo",
    srcs = ["decisions_replay_demo.py"],
    deps = mcp_requirement("mcp"),
)

py_test(
    name = "decisions_replay_demo_test",
    srcs = ["decisions_replay_demo_test.py"],
    deps = [":decisions_replay_demo"] + mcp_requirement("mcp"),
)

py_test(
    name = "decisions_request_test",
    srcs = ["decisions_request_test.py"],
    deps = [
        ":decisions_request",
        ":decisions_server",
        ":head_replay",
        "//ai/inference:manifest",
    ] + requirement("pillow") + mcp_requirement("mcp") + model_requirement("decisions", "httpx2") + model_requirement("decisions", "openai"),
)

py_library(
    name = "head_replay",
    srcs = ["head_replay.py"],
    data = [
        "//ai/models/decisions:model.textproto",
        "//config:joshua_validate_config",
        "//config/config_preset:config_presets",
    ],
    deps = [
        "//ai/inference:manifest",
        "//ai/inference:motor_policy",
        "//ai/models:registry",
        "//ai/models/decisions:adapter",
        "//config/proto:config_py_proto",
    ] + requirement("pillow") + requirement("protobuf"),
)
