load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")
load("//tools/mcp:requirements.bzl", "mcp_requirement")

py_library(
    name = "hardware_catalog_lib",
    srcs = ["hardware_catalog.py"],
    data = ["//docs/hardware_catalog:records"],
    visibility = ["//visibility:public"],
    deps = mcp_requirement("jsonschema"),
)

py_binary(
    name = "hardware_catalog_cli",
    srcs = ["hardware_catalog.py"],
    data = [
        "//docs:SUPPORTED_HARDWARE.md",
        "//docs:TESTED_HARDWARE.md",
        "//docs/hardware_catalog:guides",
    ],
    main = "hardware_catalog.py",
    deps = [":hardware_catalog_lib"],
)

py_test(
    name = "hardware_catalog_test",
    srcs = ["hardware_catalog_test.py"],
    data = [
        ":hardware_catalog_cli",
        "//docs:SUPPORTED_HARDWARE.md",
        "//docs:TESTED_HARDWARE.md",
        "//docs/hardware_catalog:guides",
    ],
    deps = [":hardware_catalog_lib"] + mcp_requirement("jsonschema"),
)
