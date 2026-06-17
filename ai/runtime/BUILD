load("@rules_python//python:defs.bzl", "py_library")
load("//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "types",
    srcs = ["types.py"],
    deps = [
        "//ai/runtime/proto:runtime_py_proto",
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
        "//ros2:ros2_type_resolver",
        "//ros2/proto:ros2_data_type_py_proto",
        "//ros2/utils:packet_parser_py",
        "//ros2/utils:qos_setting_py",
    ] + requirement("protobuf"),
)
