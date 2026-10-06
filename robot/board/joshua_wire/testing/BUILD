load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fake_joshua_wire_transport",
    testonly = True,
    hdrs = ["fake_joshua_wire_transport.h"],
    visibility = ["//robot/board:__subpackages__"],
    deps = [
        "//firmware/common:joshua_wire",
        "//robot/comm/interfaces:message_transport",
    ],
)
