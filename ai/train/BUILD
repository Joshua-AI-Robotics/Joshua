load("@rules_python//python:defs.bzl", "py_binary", "py_library")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "data_store",
    srcs = ["data_store.py"],
    deps = [
        "//ai/proto:data_store_py_proto",
    ] + requirement("datasets") + requirement("glog"),
)

py_binary(
    name = "data_load",
    srcs = ["data_load.py"],
    deps = requirement("datasets") + requirement("glog") + requirement("python-gflags"),
)

py_library(
    name = "rl",
    srcs = ["rl.py"],
    deps = [
        "//ai/proto:training_py_proto",
        "//simulation/envs:register",
    ] + requirement("gymnasium") + requirement("glog") + requirement("stable-baselines3"),
)

py_library(
    name = "mjx_envs",
    srcs = ["mjx_envs.py"],
    deps = requirement("mujoco") + requirement("mujoco-mjx") + requirement("jax") + requirement("numpy"),
)

py_library(
    name = "mjx_rl",
    srcs = ["mjx_rl.py"],
    deps = [
        ":mjx_envs",
        "//ai/proto:training_py_proto",
    ] + requirement("jax") + requirement("flax") + requirement("optax") + requirement("glog") + requirement("numpy"),
)

py_binary(
    name = "trainer",
    srcs = ["trainer.py"],
    data = [
        "//simulation:sim_models",
        "//simulation:sim_trajectories",
        "//config/config_preset:config_presets",
    ],
    deps = [
        ":rl",
        ":mjx_rl",
        "//ai/proto:training_py_proto",
        "//config/proto:config_py_proto",
    ] + requirement("python-gflags") + requirement("glog") + requirement("protobuf"),
)
