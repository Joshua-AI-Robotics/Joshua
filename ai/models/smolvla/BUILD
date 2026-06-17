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
    name = "adapter",
    srcs = ["adapter.py"],
    deps = [
        "//ai/proto:ai_model_py_proto",
        "//ai/runtime:adapter",
        "//ai/runtime:types",
        "//config/proto:config_py_proto",
        "//ros2/proto:ros2_data_type_py_proto",
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
