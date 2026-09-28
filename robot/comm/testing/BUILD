load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load(":build_boundaries_test.bzl", "transport_boundary_test")

transport_boundary_test(
    name = "transport_boundary_test",
    libraries = [
        "//robot/comm/factory:comm_factory",
        "//robot/comm/interfaces:correlated_cyclic_transport",
        "//robot/comm/interfaces:message_transport",
    ],
)

cc_library(
    name = "fake_transports",
    testonly = True,
    hdrs = ["fake_transports.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/comm/interfaces:correlated_cyclic_transport",
        "//robot/comm/interfaces:message_transport",
    ],
)

cc_library(
    name = "fake_legacy_message_transport",
    testonly = True,
    hdrs = ["fake_legacy_message_transport.h"],
    visibility = ["//visibility:public"],
    deps = ["//robot/comm/interfaces:legacy_message_transport"],
)

cc_test(
    name = "transport_contract_test",
    srcs = ["transport_contract_test.cc"],
    deps = [
        ":fake_legacy_message_transport",
        ":fake_transports",
        "@googletest//:gtest_main",
    ],
)
