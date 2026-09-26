load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "joshua_wire_board",
    srcs = ["joshua_wire_board.cc"],
    hdrs = ["joshua_wire_board.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":joshua_wire_v2_session",
        "//firmware/common:joshua_wire_v1",
        "//robot/board/frame:frame_transport",
        "//robot/board/interfaces:board_interfaces",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/factory:comm_factory",
        "//robot/comm/proto:comm_cc_proto",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_library(
    name = "joshua_wire_v2_session",
    srcs = ["joshua_wire_v2_session.cc"],
    hdrs = ["joshua_wire_v2_session.h"],
    visibility = ["//robot/board:__subpackages__"],
    deps = [
        "//firmware/common:joshua_wire_v1",
        "//firmware/common:joshua_wire_v2",
        "//robot/comm/interfaces:message_transport",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "joshua_wire_v2_session_test",
    srcs = ["joshua_wire_v2_session_test.cc"],
    deps = [
        ":joshua_wire_board",
        ":joshua_wire_v2_session",
        "//firmware/am243/joshua_dual_transport_v1:serial_commands",
        "//firmware/common:joshua_wire_serial_endpoint",
        "//firmware/common:joshua_wire_v1",
        "//firmware/common:joshua_wire_v2",
        "//robot/comm/factory:comm_factory",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "joshua_wire_board_test",
    srcs = ["joshua_wire_board_test.cc"],
    deps = [
        ":joshua_wire_board",
        "//firmware/common:joshua_wire_v1",
        "//robot/board/frame:fake_frame_transport",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/factory:comm_factory",
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
