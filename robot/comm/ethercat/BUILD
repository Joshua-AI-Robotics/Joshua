load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "ethercat_transport",
    hdrs = ["ethercat_transport.h"],
    visibility = ["//visibility:public"],
    deps = [
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "ethercat_status",
    srcs = ["ethercat_status.cc"],
    hdrs = ["ethercat_status.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":ethercat_transport",
        "@abseil-cpp//absl/status:status",
    ],
)

cc_test(
    name = "ethercat_status_test",
    srcs = ["ethercat_status_test.cc"],
    deps = [
        ":ethercat_status",
        ":ethercat_transport",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "fake_ethercat_transport",
    testonly = True,
    hdrs = ["fake_ethercat_transport.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":ethercat_transport",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_library(
    name = "soem_ethercat_transport",
    srcs = ["soem_ethercat_transport.cc"],
    hdrs = ["soem_ethercat_transport.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":ethercat_transport",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@soem",
    ],
)

cc_binary(
    name = "am243_demo_smoke",
    srcs = ["am243_demo_smoke.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":ethercat_status",
        ":ethercat_transport",
        ":soem_ethercat_transport",
        "//robot/action/motors/drivers:am243_pdo_codec",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "soem_ethercat_transport_test",
    srcs = ["soem_ethercat_transport_test.cc"],
    deps = [
        ":soem_ethercat_transport",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
