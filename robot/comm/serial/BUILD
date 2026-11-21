load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "serial_device_identity",
    srcs = ["serial_device_identity.cc"],
    hdrs = ["serial_device_identity.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "serial",
    srcs = ["serial.cc"],
    hdrs = ["serial.h"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        ":serial_io_owner",
        "//robot/comm/interfaces:byte_stream",
        "//utils:joshua_runtime",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "joshua_wire_serial_transport",
    srcs = ["joshua_wire_serial_transport.cc"],
    hdrs = ["joshua_wire_serial_transport.h"],
    implementation_deps = ["//firmware/common:joshua_wire"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        ":serial",
        "//robot/comm/interfaces:message_transport",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "caller_sized_serial_transport",
    hdrs = ["caller_sized_serial_transport.h"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        ":serial",
        "//robot/comm/interfaces:caller_sized_message_transport",
    ],
)

cc_library(
    name = "serial_config_validation",
    hdrs = ["serial_config_validation.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":serial_frame_options",
        ":serial_io_options",
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
    ],
)

cc_test(
    name = "serial_exchange_test",
    srcs = ["serial_exchange_test.cc"],
    deps = [
        ":caller_sized_serial_transport",
        ":joshua_wire_serial_transport",
        ":serial",
        "//firmware/common:joshua_wire",
        "//robot/comm/interfaces:caller_sized_message_transport",
        "//utils:joshua_runtime",
        "//utils:robot_time",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "feetech_serial_test",
    srcs = ["feetech_serial_test.cc"],
    deps = [
        "//robot/board/feetech_bus:feetech_bus_board",
        "//robot/board/feetech_bus:feetech_protocol",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/factory:comm_factory",
        "//robot/comm/proto:comm_cc_proto",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "serial_io_options",
    hdrs = ["serial_io_options.h"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "serial_io_owner",
    srcs = ["serial_io_owner.cc"],
    hdrs = ["serial_io_owner.h"],
    implementation_deps = [
        ":serial_config_validation",
        ":serial_device_identity",
        "//utils:joshua_runtime",
        "@boost.asio",
    ],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        ":serial_io_options",
        "//robot/comm/interfaces:serial_diagnostics",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "serial_io_owner_test",
    srcs = ["serial_io_owner_test.cc"],
    deps = [
        ":serial",
        ":serial_io_owner",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "serial_frame_options",
    hdrs = ["serial_frame_options.h"],
    visibility = ["//visibility:public"],
    deps = ["@abseil-cpp//absl/status:status"],
)

cc_library(
    name = "joshua_wire_frame_stream",
    srcs = ["joshua_wire_frame_stream.cc"],
    hdrs = ["joshua_wire_frame_stream.h"],
    implementation_deps = [
        "//firmware/common:joshua_wire",
        "//firmware/common:joshua_wire_stream",
    ],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        ":serial_frame_options",
        ":serial_io_owner",
        "//robot/comm/interfaces:frame_stream_transport",
    ],
)

cc_test(
    name = "joshua_wire_frame_stream_test",
    srcs = ["joshua_wire_frame_stream_test.cc"],
    deps = [
        ":joshua_wire_frame_stream",
        "//firmware/common:joshua_wire_sensor_stream",
        "@googletest//:gtest_main",
    ],
)
