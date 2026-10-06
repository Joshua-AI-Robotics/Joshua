load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:defs.bzl", "cc_test")

cc_library(
    name = "comm_factory",
    srcs = ["comm_factory.cc"],
    hdrs = ["comm_factory.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":transport_requirements",
        "//robot/comm/ethercat:ethercat_transport",
        "//robot/comm/ethercat:soem_ethercat_transport",
        "//robot/comm/interfaces:comm_lease",
        "//robot/comm/interfaces:message_framer",
        "//robot/comm/proto:comm_cc_proto",
        "//robot/comm/serial",
        "//robot/comm/serial:serial_message_transport",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
        "@boost.asio",
    ],
)

cc_library(
    name = "transport_requirements",
    srcs = ["transport_requirements.cc"],
    hdrs = ["transport_requirements.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

cc_test(
    name = "comm_factory_test",
    srcs = ["comm_factory_test.cc"],
    deps = [
        ":comm_factory",
        "//robot/comm/ethercat:fake_ethercat_transport",
        "//robot/comm/interfaces:comm_lease",
        "//robot/comm/interfaces:message_framer",
        "//robot/comm/proto:comm_cc_proto",
        "//robot/comm/testing:fake_correlated_cyclic_transport",
        "//robot/comm/testing:fake_message_transport",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:span",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "transport_requirements_test",
    srcs = ["transport_requirements_test.cc"],
    deps = [
        ":transport_requirements",
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
