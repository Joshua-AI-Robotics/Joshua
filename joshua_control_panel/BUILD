load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load(":qt_moc.bzl", "qt_moc")

package(default_visibility = ["//visibility:public"])

qt_moc(
    name = "moc_sources",
    hdrs = [
        "calibration_tab.h",
        "config_tab.h",
        "general_tab.h",
        "main_window.h",
        "monitor_tab.h",
        "operational_limit_tab.h",
    ],
)

# Generate UI headers using Qt's uic
# Adjust uic path if needed

genrule(
    name = "uic_general_tab",
    srcs = ["general_tab.ui"],
    outs = ["ui_general_tab.h"],
    cmd = "/usr/lib/qt6/libexec/uic $(location general_tab.ui) -o $@",
)

genrule(
    name = "uic_config_tab",
    srcs = ["config_tab.ui"],
    outs = ["ui_config_tab.h"],
    cmd = "/usr/lib/qt6/libexec/uic $(location config_tab.ui) -o $@",
)

genrule(
    name = "uic_monitor_tab",
    srcs = ["monitor_tab.ui"],
    outs = ["ui_monitor_tab.h"],
    cmd = "/usr/lib/qt6/libexec/uic $(location monitor_tab.ui) -o $@",
)

genrule(
    name = "uic_calibration_tab",
    srcs = ["calibration_tab.ui"],
    outs = ["ui_calibration_tab.h"],
    cmd = "/usr/lib/qt6/libexec/uic $(location calibration_tab.ui) -o $@",
)

genrule(
    name = "uic_operational_limit_tab",
    srcs = ["operational_limit_tab.ui"],
    outs = ["ui_operational_limit_tab.h"],
    cmd = "/usr/lib/qt6/libexec/uic $(location operational_limit_tab.ui) -o $@",
)

cc_library(
    name = "gui_utils",
    srcs = ["gui_utils.cc"],
    hdrs = ["gui_utils.h"],
    copts = [
        "-std=c++17",
        "-fPIC",
    ],
    includes = ["."],
    deps = ["@local_deps//:qt6_widgets"],
)

cc_library(
    name = "gui_lib",
    srcs = [
        "main_window.cc",
        "general_tab.cc",
        "config_tab.cc",
        "monitor_tab.cc",
        "calibration_tab.cc",
        "operational_limit_tab.cc",
        # Generated MOC sources
        ":moc_sources",
    ],
    hdrs = [
        "main_window.h",
        "general_tab.h",
        "config_tab.h",
        "monitor_tab.h",
        "calibration_tab.h",
        "operational_limit_tab.h",
        # Generated UI headers
        ":uic_general_tab",
        ":uic_config_tab",
        ":uic_monitor_tab",
        ":uic_calibration_tab",
        ":uic_operational_limit_tab",
    ],
    copts = [
        "-std=c++17",
        "-fPIC",
    ],
    includes = ["."],
    linkopts = ["-pthread"],
    deps = [
        ":gui_utils",
        "//config/proto:config_cc_proto",
        "//node_generator",
        "@abseil-cpp//absl/status:status",
        "@local_deps//:qt6_widgets",
        "@local_deps//:cpp_std_msgs",
        "@local_deps//:rclcpp",
    ],
)

cc_binary(
    name = "joshua_control_panel",
    srcs = ["main.cc"],
    copts = [
        "-std=c++17",
        "-fPIC",
    ],
    data = [
        "//config/config_preset:config_presets",
        # Ensure ROS2 node runfiles (including *_impl) are available when launching from the panel
        "//ros2:camera_publisher",
        "//ros2:encoder_publisher",
        "//ros2:actuator_subscriber",
        "//ros2:mock_inference",
        "//ros2:mock_inference_py",
        "//ros2:operational_limit_calibration",
        "//ros2:ros2_node_wrapper.sh",
    ],
    linkopts = ["-pthread"],
    deps = [":gui_lib"],
)
