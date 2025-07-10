load("@rules_python//python:defs.bzl", "py_library")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

package(default_visibility = ["//visibility:public"])

py_library(
    name = "ai_layer_gateway",
    srcs = ["ai_layer_gateway.py"],
    deps = [
        "//ai/policy:factory",
        requirement("protobuf"),
        requirement("numpy"),
        "//config/proto:config_py_proto",
    ],
) 
