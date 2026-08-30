load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "am243_pdo_codec",
    srcs = ["am243_pdo_codec.cc"],
    hdrs = ["am243_pdo_codec.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "am243_board",
    srcs = ["am243_board.cc"],
    hdrs = ["am243_board.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":am243_pdo_codec",
        "//firmware/common:joshua_wire_v1",
        "//robot/board/interfaces:board_interfaces",
        "//robot/board/joshua_wire:joshua_wire_board",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/ethercat:ethercat_status",
        "//robot/comm/ethercat:ethercat_transport",
        "//robot/comm/factory:comm_factory",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_binary(
    name = "am243_driver_smoke",
    srcs = ["am243_driver_smoke.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":am243_board",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/proto:comm_cc_proto",
        "@glog",
    ],
)

cc_binary(
    name = "am243_config_smoke",
    srcs = ["am243_config_smoke.cc"],
    data = ["//config/config_preset:config_presets"],
    visibility = ["//visibility:public"],
    deps = [
        "//config:config_utils",
        "//config/proto:config_cc_proto",
        "//robot/action/factory:action_factory",
        "//robot/action/proto:action_packet_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "am243_pdo_codec_test",
    srcs = ["am243_pdo_codec_test.cc"],
    deps = [
        ":am243_pdo_codec",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "am243_board_test",
    srcs = ["am243_board_test.cc"],
    deps = [
        ":am243_board",
        "//firmware/common:joshua_wire_v1",
        "//robot/board/frame:fake_frame_transport",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/ethercat:fake_ethercat_transport",
        "//robot/comm/factory:comm_factory",
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
