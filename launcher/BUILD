load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@rules_pkg//:pkg.bzl", "pkg_tar")

genrule(
    name = "version_h",
    srcs = ["//:VERSION"],
    outs = ["version.h"],
    cmd = """VERSION=$$(tr -d '\\n\\r' < '$(location //:VERSION)'); \
cat > '$@' <<EOF
#ifndef JOSHUA_LAUNCHER_VERSION_H_
#define JOSHUA_LAUNCHER_VERSION_H_
#define JOSHUA_VERSION "$$VERSION"
#endif
EOF""",
)

cc_library(
    name = "version",
    hdrs = [":version_h"],
    includes = ["."],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "simulation_launcher",
    srcs = ["simulation_launcher.cc"],
    hdrs = ["simulation_launcher.h"],
    deps = [
        "//config/proto:config_cc_proto",
        "//node_generator",
        "@glog",
    ],
)

cc_binary(
    name = "joshua_main",
    srcs = ["joshua_main.cc"],
    data = [
        "//config/config_preset:config_presets",
        "//ros2:actuator_subscriber",
        "//ros2:audio_publisher",
        "//ros2:av_check",
        "//ros2:camera_publisher",
        "//ros2:data_subscriber",
        "//ros2:home_assistant_setup",
        "//ros2:inference",
        "//ros2:lidar_publisher",
        "//ros2:media_stream_node",
        "//ros2:monitor_selftest",
        "//ros2:notification_node",
        "//ros2:position_publisher",
        "//ros2:trajectory_publisher",
        "//ros2:video_inference_node",
        "//ros2:yamnet_node",
        "//simulation",
        "//simulation:sim_models",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":simulation_launcher",
        ":version",
        "//config:config_utils",
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
        "//:VERSION",
        "//config/config_preset:config_presets",
        "//ros2:ros2_node_wrapper.sh",
    ],
    extension = "tar.gz",
    include_runfiles = True,
    mode = "0755",
    package_dir = "joshua_main",
    visibility = ["//visibility:public"],
)

# CPU-only deployment graph for the experimental monitor. The general launcher
# also bundles SmolVLA/CUDA and simulation dependencies, which are not needed here.
cc_binary(
    name = "joshua_monitor_main",
    srcs = ["joshua_main.cc"],
    data = [
        "//config/config_preset:config_presets",
        "//ros2:audio_publisher",
        "//ros2:av_check",
        "//ros2:camera_publisher",
        "//ros2:home_assistant_setup",
        "//ros2:media_stream_node",
        "//ros2:monitor_selftest",
        "//ros2:notification_node",
        "//ros2:video_inference_node",
        "//ros2:yamnet_node",
    ],
    deps = [
        ":simulation_launcher",
        ":version",
        "//config:config_utils",
        "//node_generator",
        "@gflags",
        "@glog",
    ],
)

pkg_tar(
    name = "joshua_pkg",
    srcs = [
        ":joshua_monitor_main",
        "//:VERSION",
        "//config/config_preset:config_presets",
        "//docs:BABY_MONITOR.md",
        "//ros2:ros2_node_wrapper.sh",
    ],
    extension = "tar.gz",
    include_runfiles = True,
    mode = "0755",
    package_dir = "joshua_main",
    visibility = ["//visibility:public"],
)
