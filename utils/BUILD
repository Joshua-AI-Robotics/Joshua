load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_binary(
    name = "camera_main",
    srcs = ["camera_main.cc"],
    data = ["//configs/config_preset:so100_with_dt"],
    deps = [
        "//robot/perception/camera:cv_camera",
        "//configs:config_utils",
        "//configs/proto:config_cc_proto",
        "@glog//:glog",
        "@local_deps//:core",
        "@local_deps//:highgui",
        "//robot/perception/factory:perception_factory",
        "//robot/perception/interfaces:perception_interface",
        "//robot/nexus/proto:nexus_packet_cc_proto",
    ],
)
