load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//:requirements.bzl", "requirement")

filegroup(
    name = "sim_models",
    srcs = glob(["models/**"]),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "sim_trajectories",
    srcs = glob(["trajectories/**"]),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "sim_configs",
    srcs = glob(["configs/**"]),
    visibility = ["//visibility:public"],
)

py_library(
    name = "sim_engine",
    srcs = ["sim_engine.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//simulation/proto:simulation_py_proto",
    ] + requirement("mujoco") + requirement("numpy") + requirement("glog"),
)

py_binary(
    name = "viewer",
    srcs = ["viewer.py"],
    data = [
        ":sim_configs",
        ":sim_models",
        ":sim_trajectories",
    ],
    deps = [
        ":sim_engine",
        "//simulation/modes:interactive",
        "//simulation/modes:mirror",
        "//simulation/modes:offscreen",
        "//simulation/modes:passive",
        "//simulation/modes:gym",
        "//simulation/proto:simulation_py_proto",
    ] + requirement("python-gflags") + requirement("glog") + requirement("protobuf"),
)
