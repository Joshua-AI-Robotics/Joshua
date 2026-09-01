load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "board_encoder",
    srcs = ["board_encoder.cc"],
    hdrs = ["board_encoder.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/board/interfaces:board_interfaces",
        "//robot/perception/interfaces:encoder_interface",
        "//robot/perception/proto:perception_cc_proto",
        "//robot/perception/proto:perception_packet_cc_proto",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)
