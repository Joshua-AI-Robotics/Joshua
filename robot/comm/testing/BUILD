load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fake_message_transport",
    testonly = True,
    hdrs = ["fake_message_transport.h"],
    deps = [
        "//robot/comm/interfaces:comm_lease",
        "//robot/comm/interfaces:message_transport",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_library(
    name = "fake_correlated_cyclic_transport",
    testonly = True,
    hdrs = ["fake_correlated_cyclic_transport.h"],
    deps = [
        "//robot/comm/interfaces:correlated_cyclic_transport",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/time",
        "@abseil-cpp//absl/types:span",
    ],
)

cc_test(
    name = "fakes_test",
    srcs = ["fakes_test.cc"],
    deps = [
        ":fake_correlated_cyclic_transport",
        ":fake_message_transport",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/time",
        "@googletest//:gtest_main",
    ],
)
