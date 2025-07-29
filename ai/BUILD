load("@rules_python//python:defs.bzl", "py_library")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

py_library(
    name = "ai_layer_gateway",
    srcs = ["ai_layer_gateway.py"],
    deps = [
        requirement("protobuf"),
        requirement("numpy"),
        requirement("Pillow"),
        "//config/proto:config_py_proto",
    ],
    data = [
        requirement("pandas"),
        requirement("pyarrow"),
    ],
    visibility = ["//visibility:public"],
) 
