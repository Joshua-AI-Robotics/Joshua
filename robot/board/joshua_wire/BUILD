load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "joshua_wire_board",
    srcs = ["joshua_wire_board.cc"],
    hdrs = ["joshua_wire_board.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":joshua_wire_session",
        "//firmware/common:joshua_wire_profiles",
        "//robot/board/interfaces:board_interfaces",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/factory:comm_factory",
        "//robot/comm/interfaces:message_transport",
        "//robot/comm/proto:comm_cc_proto",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "firmware_binding_test",
    srcs = ["firmware_binding_test.cc"],
    deps = [
        "//firmware/common:joshua_wire_stream",
        "//firmware/common:stepdir_test_firmware",
        "//robot/board/factory:board_factory",
        "//robot/board/joshua_wire/testing:board_config",
        "//robot/comm/factory:comm_factory",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "joshua_wire_session",
    srcs = ["joshua_wire_session.cc"],
    hdrs = ["joshua_wire_session.h"],
    visibility = ["//robot/board:__subpackages__"],
    deps = [
        "//firmware/common:joshua_wire",
        "//firmware/common:joshua_wire_commands",
        "//firmware/common:joshua_wire_profiles",
        "//robot/comm/interfaces:correlated_cyclic_transport",
        "//robot/comm/interfaces:message_transport",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "joshua_wire_board_test",
    srcs = ["joshua_wire_board_test.cc"],
    deps = [
        ":joshua_wire_board",
        "//robot/board/joshua_wire/testing:fake_joshua_wire_transport",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/factory:comm_factory",
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "joshua_wire_session_test",
    srcs = ["joshua_wire_session_test.cc"],
    deps = [
        ":joshua_wire_board",
        ":joshua_wire_session",
        "//firmware/am243/joshua_dual_transport:commands",
        "//firmware/common:joshua_wire_endpoint",
        "//firmware/common:joshua_wire_profiles",
        "//robot/comm/factory:comm_factory",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)
