load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "board_interfaces",
    hdrs = [
        "board_channel.h",
        "board_interface.h",
    ],
    deps = [
        "//robot/board/proto:board_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)
