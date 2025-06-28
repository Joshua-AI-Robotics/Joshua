package(default_visibility = ["//visibility:public"])

load("@rules_python//python:defs.bzl", "py_library")
load("@ubirobotics_pip_deps//:requirements.bzl", "requirement")

py_library(
    name = "model",
    srcs = ["model.py"],
    deps = [
        "//robot/nexus/proto:nexus_packet_py_proto",
        requirement("protobuf"),
    ],
) 