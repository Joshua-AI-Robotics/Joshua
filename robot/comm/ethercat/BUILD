load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "ethercat_transport",
    hdrs = ["ethercat_transport.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":ethercat_types",
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
        ":ethercat_types",
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
    implementation_deps = ["@soem"],
    visibility = ["//robot/comm:__subpackages__"],
    deps = [
        ":ethercat_master",
        ":ethercat_transport",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
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
        "//robot/board/am243:am243_pdo_codec",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

cc_test(
    name = "soem_ethercat_transport_test",
    srcs = ["soem_ethercat_transport_test.cc"],
    deps = [
        ":ethercat_master",
        ":soem_ethercat_transport",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)

cc_library(
    name = "ethercat_types",
    hdrs = ["ethercat_types.h"],
    visibility = ["//robot/comm:__subpackages__"],
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

cc_test(
    name = "soem_ethercat_backend_test",
    srcs = ["soem_ethercat_backend_test.cc"],
    deps = [
        ":coe_sdo_transfer",
        ":ethercat_master",
        ":ethercat_types",
        ":soem_ethercat_backend",
        "//firmware/common:joshua_wire_ethercat",
        "@googletest//:gtest_main",
    ],
)
