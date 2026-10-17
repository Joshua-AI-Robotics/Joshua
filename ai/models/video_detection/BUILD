load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:defs.bzl", "py_library")
load("//:requirements.bzl", "requirement")
load("//ai/models/yamnet:requirements.bzl", "monitor_requirement")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "video_detection_config_proto",
    srcs = ["video_detection_config.proto"],
)

genrule(
    name = "model_asset",
    srcs = ["@efficientdet_tflite//file"],
    outs = ["efficientdet_lite0.tflite"],
    cmd = "cp $< $@",
)

py_library(
    name = "processor",
    srcs = ["processor.py"],
    data = [":model_asset"],
    deps = monitor_requirement("ai_edge_litert") + requirement("numpy") + requirement("pillow"),
)
