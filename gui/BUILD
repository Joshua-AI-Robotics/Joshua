load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load(":qt_moc.bzl", "qt_moc")

package(default_visibility = ["//visibility:public"]) 

qt_moc(
    name = "moc_sources",
    hdrs = [
        "main_window.h",
        "control_panel.h",
        "status_panel.h",
    ],
)

cc_library(
    name = "gui_lib",
    srcs = [
        "main_window.cc",
        "control_panel.cc",
        "status_panel.cc",
        # Generated MOC sources
        ":moc_sources",
    ],
    hdrs = [
        "main_window.h",
        "control_panel.h",
        "status_panel.h",
    ],
    copts = ["-std=c++17", "-fPIC"],
    deps = [
        "@local_deps//:qt6_widgets",
    ],
)

cc_binary(
    name = "gui_app",
    srcs = ["main.cc"],
    copts = ["-std=c++17", "-fPIC"],
    deps = [":gui_lib"],
) 