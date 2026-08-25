load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "mock_board",
    srcs = ["mock_board.cc"],
    hdrs = ["mock_board.h"],
    deps = [
        "//robot/board/interfaces:board_interfaces",
        "//robot/board/proto:board_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)
