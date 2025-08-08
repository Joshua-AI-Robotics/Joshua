load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load(":qt_moc.bzl", "qt_moc")

package(default_visibility = ["//visibility:public"]) 

qt_moc(
    name = "moc_sources",
    hdrs = [
        "main_window.h",
        "general_tab.h",
        "config_tab.h",
        "monitor_tab.h",
        "calibration_tab.h",
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

cc_library(
    name = "gui_lib",
    srcs = [
        "main_window.cc",
        "general_tab.cc",
        "config_tab.cc",
        "monitor_tab.cc",
        "calibration_tab.cc",
        # Generated MOC sources
        ":moc_sources",
    ],
    hdrs = [
        "main_window.h",
        "general_tab.h",
        "config_tab.h",
        "monitor_tab.h",
        "calibration_tab.h",
        # Generated UI headers
        ":uic_general_tab",
        ":uic_config_tab",
        ":uic_monitor_tab",
        ":uic_calibration_tab",
    ],
    includes = ["."],
    copts = ["-std=c++17", "-fPIC"],
    linkopts = ["-pthread"],
    deps = [
        "@local_deps//:qt6_widgets",
        "//config/proto:config_cc_proto",
        "//node_generator:node_generator",
    ],
)

cc_binary(
    name = "gui_app",
    srcs = ["main.cc"],
    data = [
        "//config/config_preset:config_presets",
    ],
    copts = ["-std=c++17", "-fPIC"],
    linkopts = ["-pthread"],
    deps = [":gui_lib"],
)
