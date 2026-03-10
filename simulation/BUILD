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

py_library(
    name = "mujoco_engine",
    srcs = ["mujoco_engine.py"],
    visibility = ["//visibility:public"],
    deps = [
        "//simulation/proto:simulation_py_proto",
    ] + requirement("mujoco") + requirement("numpy") + requirement("glog"),
)

py_binary(
    name = "simulation",
    srcs = ["main.py"],
    main = "main.py",
    visibility = ["//visibility:public"],
    data = [
        ":sim_models",
        ":sim_trajectories",
        "//config/config_preset:config_presets",
    ],
    deps = [
        ":mujoco_engine",
        "//simulation/modes:interactive",
        "//simulation/modes:mirror",
        "//simulation/modes:offscreen",
        "//simulation/modes:passive",
        "//simulation/proto:simulation_py_proto",
        "//config/proto:config_py_proto",
    ] + requirement("python-gflags") + requirement("glog") + requirement("protobuf"),
)
