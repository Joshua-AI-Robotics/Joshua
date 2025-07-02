load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

cc_binary(
    name = "camera_main",
    srcs = ["camera_main.cc"],
    data = ["//robot/config:robot_config_pbtxt"],
    deps = [
        "//robot/config:config_utils",
        "//robot/perception/factory:perception_factory",
        "//robot/perception/interfaces:perception_interface",
        "//robot/nexus/proto:nexus_packet_cc_proto",
        "@com_github_google_glog//:glog",
        "@opencv//:opencv",
    ],
)
