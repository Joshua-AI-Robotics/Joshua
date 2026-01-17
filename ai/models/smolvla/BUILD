load("@project_joshua_pip_deps//:requirements.bzl", "requirement")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:defs.bzl", "py_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "smolvla_config_proto",
    srcs = ["smolvla_config.proto"],
)

py_proto_library(
    name = "smolvla_config_py_proto",
    visibility = ["//visibility:public"],
    deps = [":smolvla_config_proto"],
)

py_library(
    name = "smolvla",
    srcs = ["smolvla.py"],
    deps = [
        "//ai/models:model_base",
        ":smolvla_config_py_proto",
        "//ros2:image_converter",
        requirement("numpy"),
        requirement("scipy"),
        requirement("protobuf"),
        requirement("torch"),
        requirement("transformers"),
        requirement("lerobot"),
        requirement("draccus"),
        requirement("num2words"),
        requirement("pillow"),
        requirement("opencv-python"),
        requirement("sympy"),
        requirement("glog"),
    ],
)
