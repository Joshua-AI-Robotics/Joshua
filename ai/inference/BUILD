load("@rules_python//python:defs.bzl", "py_library")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "types",
    srcs = ["types.py"],
    deps = [
        "//ai/inference/proto:inference_py_proto",
    ] + requirement("protobuf"),
)

py_library(
    name = "adapter",
    srcs = ["adapter.py"],
    deps = [
        ":types",
        "//ai/proto:ai_model_py_proto",
        "//config/proto:config_py_proto",
    ] + requirement("protobuf"),
)

py_library(
    name = "manifest",
    srcs = ["manifest.py"],
    deps = [
        ":adapter",
        "//ai/proto:ai_model_py_proto",
        "//ai/inference/proto:model_manifest_py_proto",
    ] + requirement("protobuf"),
)

py_library(
    name = "environment_manager",
    srcs = ["environment_manager.py"],
    deps = [
        ":manifest",
        "//ai/inference/proto:model_manifest_py_proto",
    ],
)

py_library(
    name = "observation_codec",
    srcs = ["observation_codec.py"],
    deps = [
        ":types",
        "//ros2:image_converter",
        "//ros2/proto:ros2_data_type_py_proto",
    ] + requirement("numpy"),
)

py_library(
    name = "host",
    srcs = ["host.py"],
    deps = [
        ":adapter",
        ":observation_codec",
        ":types",
        "//ai/models:registry",
        "//ai/proto:ai_model_py_proto",
        "//config/proto:config_py_proto",
        "//robot/action/proto:action_py_proto",
        "//ros2:ros2_type_resolver",
        "//ros2/proto:ros2_data_type_py_proto",
        "//ros2/utils:packet_parser_py",
        "//ros2/utils:qos_setting_py",
    ] + requirement("protobuf"),
)

py_library(
    name = "host_main",
    srcs = ["host_main.py"],
    deps = [
        ":host",
        "//ros2/utils:ros_python_paths_py",
        "//ros2:node_runner_py",
    ],
)

py_library(
    name = "inference_launcher",
    srcs = ["inference_launcher.py"],
    data = [
        "//:requirements.lock",
        "//:requirements_3.12.lock",
        "//ai/models:sources",
        "//ai/models:manifests",
        "//ai/models:locks",
    ],
    deps = [
        ":environment_manager",
        ":host_main",
        ":manifest",
        "//ai/inference/proto:model_manifest_py_proto",
        "//config/proto:config_py_proto",
    ] + requirement("protobuf"),
)
