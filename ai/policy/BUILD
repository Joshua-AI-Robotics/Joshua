load("@rules_python//python:defs.bzl", "py_library")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "factory",
    srcs = ["factory.py"],
    deps = [
        ":base_policy",
        "//ai/configs:base_policy_config",
        "//ai/policy/pi0",
        "//ai/policy/tdmpc",
    ],
)

py_library(
    name = "base_policy",
    srcs = ["base_policy.py"],
    deps = [
        requirement("protobuf"),
    ],
)

