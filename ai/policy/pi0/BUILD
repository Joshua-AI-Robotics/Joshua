load("@rules_python//python:defs.bzl", "py_library")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "pi0",
    deps = [
        ":pi0_config",
        ":pi0_policy",
    ],
)

py_library(
    name = "pi0_policy",
    srcs = ["pi0_policy.py"],
    deps = [
        "//ai/policy:base_policy",
    ],
)

py_library(
    name = "pi0_config",
    srcs = ["pi0_config.py"],
    deps = [
        "//ai/configs:base_policy_config",
    ],
)