load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_python//python:proto.bzl", "py_proto_library")

# proto definitions.
proto_library(
    name = "config_proto",
    srcs = ["config.proto"],
    visibility = ["//visibility:public"],
    deps = [
        ":ai_proto",
        ":calibration_proto",
        ":robot_proto",
    ],
)

proto_library(
    name = "robot_proto",
    srcs = ["robot.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/action/proto:action_proto",
        "//robot/perception/proto:perception_proto",
    ],
)

proto_library(
    name = "ai_proto",
    srcs = ["ai.proto"],
    deps = [
        "//ai/proto:ai_model_proto",
        "//ai/proto:data_store_proto",
    ],
    visibility = ["//visibility:public"],
)

proto_library(
    name = "calibration_proto",
    srcs = ["calibration.proto"],
    visibility = ["//visibility:public"],
)

proto_library(
    name = "physical_layout_proto",
    srcs = ["physical_layout.proto"],
    visibility = ["//visibility:public"],
)

# cc proto definitions.
cc_proto_library(
    name = "config_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":config_proto"],
)

cc_proto_library(
    name = "robot_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":robot_proto"],
)

cc_proto_library(
    name = "ai_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":ai_proto"],
)

cc_proto_library(
    name = "calibration_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":calibration_proto"],
)

cc_proto_library(
    name = "physical_layout_cc_proto",
    visibility = ["//visibility:public"],
    deps = [":physical_layout_proto"],
)

# py proto definitions.
py_proto_library(
    name = "config_py_proto",
    visibility = ["//visibility:public"],
    deps = [":config_proto"],
)
