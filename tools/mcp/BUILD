load(":requirements.bzl", "mcp_requirement")
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
    data = [
        "//docs:hardware_catalog.json",
        "//docs:hardware_test_records.json",
    ],
    deps = mcp_requirement("mcp"),
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
        "//docs:hardware_tests/sunplus_spca2281_1080p_2026-11-04.md",
        "//docs:hardware_tests/sunplus_spca2281_2026-11-04.md",
        "//robot/perception/camera:cv_camera.cc",
    ],
    deps = [":config_validation_lib"],
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
        "//node_generator:observation_fixture",
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
