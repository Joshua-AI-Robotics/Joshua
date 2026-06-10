load("@rules_python//python:defs.bzl", "py_binary")
load("//:requirements.bzl", "requirement")

filegroup(
    name = "sim_models",
    srcs = glob(["models/**"]),
    visibility = ["//visibility:public"],
)

py_binary(
    name = "simulation",
    srcs = ["main.py"],
    main = "main.py",
    visibility = ["//visibility:public"],
    data = [
        ":sim_models",
        "//config/config_preset:config_presets",
    ],
    deps = [
        "//simulation/isaac:launcher",
        "//simulation/mujoco:engine",
        "//simulation/mujoco/modes:interactive",
        "//simulation/mujoco/modes:mirror",
        "//simulation/mujoco/modes:offscreen",
        "//simulation/mujoco/modes:passive",
        "//simulation/proto:simulation_py_proto",
        "//config/proto:config_py_proto",
    ] + requirement("python-gflags") + requirement("glog") + requirement("protobuf"),
)
