load("@rules_python//python:defs.bzl", "py_library")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "factory",
    srcs = ["factory.py"],
    deps = [
        "//ai/policy/decision_transformer",
        # TODO: Add other policies here.
    ],
)
