load("//ai/models:requirements.bzl", "model_requirement")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")
load("//:requirements.bzl", "requirement")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:defs.bzl", "py_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

exports_files([
    "model.textproto",
    "requirements.in",
    "requirements.lock",
    "requirements_3.12.lock",
])

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
    data = [
        "model.textproto",
        "requirements.lock",
    ],
    deps = [
        "//ai/proto:ai_model_py_proto",
        "//ai/inference:adapter",
        "//ai/inference:types",
        "//config/proto:config_py_proto",
        "//ros2/proto:ros2_data_type_py_proto",
        requirement("protobuf"),
    ] + model_requirement("smolvla", "torch")
    + model_requirement("smolvla", "transformers")
    + model_requirement("smolvla", "lerobot")
    + model_requirement("smolvla", "draccus")
    + model_requirement("smolvla", "num2words")
    + model_requirement("smolvla", "pillow")
    + model_requirement("smolvla", "scipy")
    + model_requirement("smolvla", "sympy"),
    name = "smolvla",
    srcs = ["smolvla.py"],
    deps = [
        ":smolvla_config_py_proto",
        "//ai/models:model_base",
        "//robot/action/proto:action_packet_py_proto",
        "//ros2:image_converter",
    ] + requirement("numpy") + requirement("scipy") + requirement("protobuf") + requirement("torch") + requirement("transformers") + requirement("lerobot") + requirement("draccus") + requirement("num2words") + requirement("pillow") + requirement("opencv-python") + requirement("sympy") + requirement("glog"),
)
