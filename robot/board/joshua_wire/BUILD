load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "joshua_wire_board",
    srcs = ["joshua_wire_board.cc"],
    hdrs = ["joshua_wire_board.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":joshua_wire_v1_framer",
        "//firmware/common:joshua_wire_v1",
        "//robot/board/frame:frame_transport",
        "//robot/board/interfaces:board_interfaces",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/factory:comm_factory",
        "//robot/comm/factory:transport_requirements",
        "//robot/comm/proto:comm_cc_proto",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "joshua_wire_v1_framer",
    srcs = ["joshua_wire_v1_framer.cc"],
    hdrs = ["joshua_wire_v1_framer.h"],
    deps = [
        "//firmware/common:joshua_wire_v1",
        "//robot/comm/interfaces:message_framer",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "joshua_wire_v1_framer_test",
    srcs = ["joshua_wire_v1_framer_test.cc"],
    deps = [
        ":joshua_wire_v1_framer",
        "//firmware/common:joshua_wire_v1",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "joshua_wire_board_test",
    srcs = ["joshua_wire_board_test.cc"],
    deps = [
        ":joshua_wire_board",
        "//firmware/common:joshua_wire_v1",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/factory:comm_factory",
        "//robot/comm/interfaces:comm_lease",
        "//robot/comm/proto:comm_cc_proto",
        "//robot/comm/testing:fake_message_transport",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)
