load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@rules_pkg//:pkg.bzl", "pkg_tar")

cc_binary(
    name = "joshua_main",
    srcs = ["joshua_main.cc"],
    data = [
        "//config/config_preset:config_presets",
        "//ros2:actuator_subscriber",
        "//ros2:actuator_subscriber_py",
        "//ros2:camera_publisher",
        "//ros2:encoder_publisher",
        "//ros2:lidar_publisher",
        "//ros2:inference",
        "//ros2:operational_limit_calibration",
        "//ros2:data_subscriber",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//node_generator",
        "@gflags",
        "@glog",
    ],
)

# Package the native binary and runtime libs into a tar archive
pkg_tar(
    name = "joshua_main_pkg",
    srcs = [
        ":joshua_main",
        "//config/config_preset:config_presets",
        "//ros2:ros2_node_wrapper.sh",
    ],
    extension = "tar.gz",
    include_runfiles = True,
    mode = "0755",
    package_dir = "joshua_main",
    visibility = ["//visibility:public"],
)
