load(":requirements.bzl", "mcp_requirement")
load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")

exports_files([
    "requirements.in",
    "requirements.lock",
    "requirements_3.12.lock",
])

py_library(
    name = "config_validation_lib",
    srcs = ["config_validation_server.py"],
    deps = mcp_requirement("mcp"),
)

py_binary(
    name = "config_validation_server",
    srcs = ["main.py"],
    main = "main.py",
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
