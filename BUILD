load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@ubirobotics_pip_deps//:requirements.bzl", "requirement")

cc_binary(
    name = "main_program",
    srcs = ["main.cc"],
    data = [
        "//robot/config:robot_config_pbtxt",
    ],
    deps = [
        "//robot/actuation/factory:actuation_factory",
        "//robot/config:config_utils",
        "//robot/perception/factory:perception_factory",
        "//robot/perception/interfaces:perception_interface",
        "//robot/nexus:nexus",
        "@com_github_google_glog//:glog",
        "@com_github_gflags_gflags//:gflags",
        "@rules_python//python/cc:current_py_cc_libs",
    ],
    visibility = ["//visibility:public"],
)