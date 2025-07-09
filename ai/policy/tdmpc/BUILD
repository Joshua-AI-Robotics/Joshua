load("@rules_python//python:defs.bzl", "py_library")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "tdmpc",
    deps = [
        ":tdmpc_config",
        ":tdmpc_policy",
    ],
)

py_library(
    name = "tdmpc_policy",
    srcs = ["tdmpc_policy.py"],
    deps = [
        "//ai/policy:base_policy",
    ],
)

py_library(
    name = "tdmpc_config",
    srcs = ["tdmpc_config.py"],
    deps = [
        "//ai/configs:base_policy_config",
    ],
)
