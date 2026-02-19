load("@rules_python//python:defs.bzl", "py_library")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "interactive",
    srcs = ["interactive.py"],
    deps = [
        "//simulation:sim_engine",
    ] + requirement("mujoco"),
)

py_library(
    name = "passive",
    srcs = ["passive.py"],
    deps = [
        "//simulation:sim_engine",
        "//simulation/proto:simulation_py_proto",
    ] + requirement("mujoco") + requirement("numpy") + requirement("glog"),
)

py_library(
    name = "mirror",
    srcs = ["mirror.py"],
    deps = [
        "//simulation:sim_engine",
        "//simulation/proto:simulation_py_proto",
    ] + requirement("mujoco") + requirement("numpy") + requirement("glog"),
)

py_library(
    name = "offscreen",
    srcs = ["offscreen.py"],
    deps = [
        "//simulation:sim_engine",
        "//simulation/proto:simulation_py_proto",
    ] + requirement("glog") + requirement("numpy") + requirement("pillow"),
)

py_library(
    name = "gym",
    srcs = ["gym.py"],
    deps = [
        "//simulation:sim_engine",
        "//simulation/envs:register",
        "//simulation/proto:simulation_py_proto",
    ] + requirement("gymnasium") + requirement("glog") + requirement("stable-baselines3"),
)
