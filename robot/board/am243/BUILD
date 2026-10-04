load("@rules_cc//cc:defs.bzl", "cc_test")

cc_test(
    name = "am243_board_test",
    srcs = ["am243_board_test.cc"],
    deps = [
        "//firmware/common:joshua_wire_v1",
        "//robot/board/factory:board_factory",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/factory:comm_factory",
        "//robot/comm/proto:comm_cc_proto",
        "//robot/comm/testing:fake_legacy_message_transport",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
