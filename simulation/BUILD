load("@rules_python//python:defs.bzl", "py_binary")
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

py_binary(
    name = "viewer",
    srcs = ["viewer.py"],
    data = [
        ":sim_models",
        ":sim_trajectories",
    ],
    deps = requirement("mujoco") + requirement("numpy") +
           requirement("python-gflags") + requirement("glog"),
)
