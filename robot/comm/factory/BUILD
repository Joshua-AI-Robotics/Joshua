load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:defs.bzl", "cc_test")

cc_library(
    name = "comm_factory",
    srcs = ["comm_factory.cc"],
    hdrs = ["comm_factory.h"],
    implementation_deps = [
        "//robot/comm/ethercat:soem_ethercat_transport",
        "//robot/comm/serial",
        "//robot/comm/serial:framed_serial_transport",
        "@boost.asio",
        "@glog",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/comm/ethercat:ethercat_transport",
        "//robot/comm/interfaces:byte_stream",
        "//robot/comm/interfaces:correlated_cyclic_transport",
        "//robot/comm/interfaces:message_transport",
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "comm_factory_test",
    srcs = ["comm_factory_test.cc"],
    deps = [
        ":comm_factory",
        "//robot/comm/ethercat:fake_ethercat_transport",
        "//robot/comm/proto:comm_cc_proto",
        "//robot/comm/testing:fake_transports",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
