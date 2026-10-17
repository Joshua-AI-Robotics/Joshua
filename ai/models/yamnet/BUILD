load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:defs.bzl", "py_library")
load("//:requirements.bzl", "requirement")
load("//ai/models/yamnet:requirements.bzl", "monitor_requirement")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "yamnet_config_proto",
    srcs = ["yamnet_config.proto"],
)

genrule(
    name = "model_asset",
    srcs = ["@yamnet_tflite//file"],
    outs = ["yamnet.tflite"],
    cmd = "cp $< $@",
)

py_library(
    name = "processor",
    srcs = ["processor.py"],
    data = [":model_asset"],
    deps = monitor_requirement("ai_edge_litert") + requirement("numpy") + requirement("pillow"),
)
