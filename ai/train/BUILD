load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "data_store",
    srcs = ["data_store.py"],
    deps = [
        requirement("datasets"),
    ],
)