load("@rules_python//python:defs.bzl", "py_library")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "mjx_envs",
    srcs = glob(["*.py"]),
    deps = requirement("mujoco") + requirement("mujoco-mjx") + requirement("jax") + requirement("numpy"),
)
