load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")
load("@rules_pkg//:pkg.bzl", "pkg_tar")

cc_library(
    name = "node_generator",
    srcs = ["node_generator.cc"],
    hdrs = ["node_generator.h"],
    deps = [
        "//config:config_utils",
        "@glog//:glog",
    ],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "joshua_main",
    srcs = ["joshua_main.cc"],
    data = [
        "//config/config_preset:config_presets",
        "//ros2:ros2_executables",
    ],
    deps = [
        ":node_generator",
        "@glog//:glog",
        "@gflags//:gflags",
    ],
    visibility = ["//visibility:public"],
)

# Package the native binary and runtime libs into a tar archive
pkg_tar(
    name = "joshua_main_tar",
    srcs = [
        ":joshua_main",
        "//config/config_preset:config_presets",
    ],
    extension = ".gz",
    package_dir = "joshua_main",
    include_runfiles = True,
    mode = "0755",
    visibility = ["//visibility:public"],
)
