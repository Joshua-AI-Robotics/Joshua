load("@rules_python//python:defs.bzl", "py_library")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "task",
    srcs = ["task.py"],
    deps = requirement("mujoco") + requirement("numpy"),
)

py_library(
    name = "base_env",
    srcs = ["base_env.py"],
    deps = [
        ":task",
    ] + requirement("gymnasium") + requirement("mujoco") + requirement("numpy"),
)

py_library(
    name = "reach",
    srcs = ["reach.py"],
    deps = requirement("mujoco") + requirement("numpy"),
)

py_library(
    name = "pick_place",
    srcs = ["pick_place.py"],
    deps = requirement("mujoco") + requirement("numpy"),
)

py_library(
    name = "register",
    srcs = ["register.py"],
    deps = [
        ":base_env",
        ":reach",
        ":pick_place",
    ] + requirement("gymnasium"),
)
