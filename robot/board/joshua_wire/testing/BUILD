load("@rules_cc//cc:defs.bzl", "cc_library")

cc_library(
    name = "fake_joshua_wire_transport",
    testonly = True,
    hdrs = ["fake_joshua_wire_transport.h"],
    visibility = ["//robot/board:__subpackages__"],
    deps = [
        "//firmware/am243/joshua_dual_transport:commands",
        "//firmware/common:joshua_wire",
        "//firmware/common:joshua_wire_endpoint",
        "//robot/comm/interfaces:message_transport",
    ],
)
