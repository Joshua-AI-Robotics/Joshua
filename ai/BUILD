package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")
load("@ubirobotics_pip_deps//:requirements.bzl", "requirement")

py_library(
    name = "ai_layer_gateway",
    srcs = ["ai_layer_gateway.py"],
    deps = [
        ":base_ai_layer",
    ],
) 

py_library(
    name = "base_ai_layer",
    srcs = ["base_ai_layer.py"],
    deps = [
        "//robot/nexus/proto:nexus_packet_py_proto",
        requirement("protobuf"),
        requirement("numpy"),
    ],
)