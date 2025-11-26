load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "model_base",
    srcs = ["model_base.py"],
    deps = [
        requirement("numpy"),
        requirement("protobuf"),
    ],
)
