load("@rules_python//python:defs.bzl", "py_test")

filegroup(
    name = "templates",
    srcs = glob([
        "*.md",
        "*.service",
        "*.sh",
        "*.timer",
        "*.yaml",
        "install_microphone_identity.py",
        "network_recovery.py",
    ]),
    visibility = ["//visibility:public"],
)

py_test(
    name = "network_recovery_test",
    srcs = [
        "network_recovery.py",
        "network_recovery_test.py",
    ],
    imports = ["."],
)
