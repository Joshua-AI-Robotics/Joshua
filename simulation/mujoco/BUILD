load("@rules_python//python:defs.bzl", "py_library")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "engine",
    srcs = [
        "__init__.py",
        "engine.py",
    ],
    deps = [
        "//simulation/proto:simulation_py_proto",
    ] + requirement("mujoco") + requirement("numpy") + requirement("glog"),
)
