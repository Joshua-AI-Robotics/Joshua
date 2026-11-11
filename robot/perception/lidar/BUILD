load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "lds01_driver",
    srcs = ["lds01_driver.cc"],
    hdrs = ["lds01_driver.h"],
    deps = [
        "//robot/comm/interfaces:byte_stream",
        "//robot/perception/interfaces:lidar_interface",
        "//robot/perception/proto:perception_cc_proto",
        "//robot/perception/proto:perception_packet_cc_proto",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@glog",
    ],
)

cc_test(
    name = "lds01_driver_test",
    srcs = ["lds01_driver_test.cc"],
    deps = [
        ":lds01_driver",
        "//robot/comm/interfaces:byte_stream",
        "//robot/perception/proto:perception_cc_proto",
        "@abseil-cpp//absl/status:status",
        "@googletest//:gtest_main",
    ],
)
