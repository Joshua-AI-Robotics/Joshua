load("@rules_python//python:defs.bzl", "py_library")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "ai_layer_gateway",
    srcs = ["ai_layer_gateway.py"],
    deps = [
        ":base_ai_layer",
        "//ai/policy:factory",
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