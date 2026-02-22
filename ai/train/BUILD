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
    name = "mjx_rl",
    srcs = ["mjx_rl.py"],
    deps = [
        "//ai/train/mjx_envs",
        "//ai/proto:training_py_proto",
    ] + requirement("jax") + requirement("flax") + requirement("optax") + requirement("glog") + requirement("numpy") + requirement("mujoco"),
)

py_library(
    name = "isaac_launcher",
    srcs = ["isaac_launcher.py"],
    data = [
        "isaac_runner.py",
        "//simulation:sim_models",
    ] + glob(["isaac_tasks/**/*.py"]),
    deps = [
        "//ai/proto:training_py_proto",
    ] + requirement("glog"),
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
        ":mjx_rl",
        ":isaac_launcher",
        "//ai/proto:training_py_proto",
        "//config/proto:config_py_proto",
    ] + requirement("python-gflags") + requirement("glog") + requirement("protobuf"),
)
