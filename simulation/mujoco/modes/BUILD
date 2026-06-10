load("@rules_python//python:defs.bzl", "py_library")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "interactive",
    srcs = ["interactive.py"],
    deps = [
        "//simulation/mujoco:engine",
    ] + requirement("mujoco"),
)

py_library(
    name = "passive",
    srcs = ["passive.py"],
    deps = [
        "//simulation/mujoco:engine",
        "//simulation/proto:simulation_py_proto",
    ] + requirement("mujoco") + requirement("numpy") + requirement("glog"),
)

py_library(
    name = "mirror",
    srcs = ["mirror.py"],
    deps = [
        "//simulation/mujoco:engine",
        "//simulation/proto:simulation_py_proto",
        "//ros2:ros2_type_resolver",
    ] + requirement("mujoco") + requirement("numpy") + requirement("glog"),
)

py_library(
    name = "offscreen",
    srcs = ["offscreen.py"],
    deps = [
        "//simulation/mujoco:engine",
        "//simulation/proto:simulation_py_proto",
    ] + requirement("glog") + requirement("numpy") + requirement("pillow"),
)
