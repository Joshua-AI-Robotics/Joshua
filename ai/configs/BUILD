load("@rules_python//python:defs.bzl", "py_library")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "base_policy_config",
    srcs = ["base_policy_config.py"],
    deps = [
        requirement("protobuf"),
    ],
)
