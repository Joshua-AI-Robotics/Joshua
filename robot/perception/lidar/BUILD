load("@rules_cc//cc:defs.bzl", "cc_library")
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "lds01_driver",
    hdrs = ["lds01_driver.h"],
    srcs = ["lds01_driver.cc"],
    deps = [
        "@abseil-cpp//absl/status:status",
        "//robot/comm/serial:serial",
        "//config/proto:robot_cc_proto",
        "//robot/perception/interfaces:lidar_interface",
        "//robot/perception/proto:perception_packet_cc_proto",
        "@glog//:glog",
        "@boost.asio",
    ],
)
