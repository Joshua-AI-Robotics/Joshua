load("@rules_python//python:defs.bzl", "py_library")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "launcher",
    srcs = [
        "__init__.py",
        "launcher.py",
    ],
    # viewer.py runs in Isaac Lab's own Python environment as a
    # subprocess; it is shipped as data, not analyzed as a Bazel source.
    data = [
        "viewer.py",
        "//simulation:sim_models",
    ],
    deps = [
        "//simulation/proto:simulation_py_proto",
    ] + requirement("glog"),
)
