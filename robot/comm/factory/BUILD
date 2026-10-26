load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_cc//cc:defs.bzl", "cc_test")

cc_library(
    name = "comm_config_validation",
    hdrs = ["comm_config_validation.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//firmware/common:joshua_wire_ethercat",
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
    ],
)

cc_library(
    name = "comm_factory",
    srcs = ["comm_factory.cc"],
    hdrs = ["comm_factory.h"],
    implementation_deps = [
        ":comm_config_validation",
        "//robot/comm/ethercat:ethercat_master",
        "//robot/comm/ethercat:joshua_wire_ethercat_transport",
        "//robot/comm/ethercat:soem_ethercat_backend",
        "//robot/comm/serial",
        "//robot/comm/serial:caller_sized_serial_transport",
        "//robot/comm/serial:joshua_wire_serial_transport",
        "//robot/comm/serial:serial_config_validation",
        "//utils:status_macros",
        "@boost.asio",
        "@glog",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//robot/comm/interfaces:byte_stream",
        "//robot/comm/interfaces:correlated_cyclic_transport",
        "//robot/comm/interfaces:message_transport",
        "//robot/comm/proto:comm_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
    ],
)

cc_test(
    name = "comm_factory_test",
    srcs = ["comm_factory_test.cc"],
    deps = [
        ":comm_factory",
        "//robot/comm/interfaces:caller_sized_message_transport",
        "//robot/comm/proto:comm_cc_proto",
        "//robot/comm/testing:fake_transports",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
