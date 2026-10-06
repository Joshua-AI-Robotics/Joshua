load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

cc_library(
    name = "ethercat_types",
    hdrs = ["ethercat_types.h"],
    visibility = ["//robot/comm:__subpackages__"],
)

cc_library(
    name = "ethercat_status",
    srcs = ["ethercat_status.cc"],
    hdrs = ["ethercat_status.h"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        ":ethercat_types",
        "@abseil-cpp//absl/status:status",
    ],
)

cc_test(
    name = "ethercat_status_test",
    srcs = ["ethercat_status_test.cc"],
    deps = [
        ":ethercat_status",
        ":ethercat_types",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "ethercat_master",
    srcs = ["ethercat_master.cc"],
    hdrs = ["ethercat_master.h"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        ":ethercat_status",
        ":ethercat_types",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "coe_sdo_transfer",
    srcs = ["coe_sdo_transfer.cc"],
    hdrs = ["coe_sdo_transfer.h"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "soem_ethercat_backend",
    srcs = ["soem_ethercat_backend.cc"],
    hdrs = ["soem_ethercat_backend.h"],
    implementation_deps = ["@soem"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        ":coe_sdo_transfer",
        ":ethercat_master",
        ":ethercat_types",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "joshua_wire_ethercat_transport",
    srcs = ["joshua_wire_ethercat_transport.cc"],
    hdrs = ["joshua_wire_ethercat_transport.h"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        ":ethercat_master",
        ":ethercat_types",
        "//firmware/common:joshua_wire_ethercat",
        "//robot/comm/interfaces:correlated_cyclic_transport",
        "//robot/comm/interfaces:message_transport",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "soem_ethercat_backend_test",
    srcs = ["soem_ethercat_backend_test.cc"],
    deps = [
        ":coe_sdo_transfer",
        ":ethercat_master",
        ":ethercat_types",
        ":joshua_wire_ethercat_transport",
        ":soem_ethercat_backend",
        "//firmware/am243/joshua_dual_transport:commands",
        "//firmware/common:joshua_ethercat_profile",
        "//firmware/common:joshua_wire_ethercat",
        "//robot/board/factory:board_factory",
        "//robot/board/proto:board_cc_proto",
        "//robot/comm/factory:comm_factory",
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
