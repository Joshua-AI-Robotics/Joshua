load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@project_joshua_pip_deps//:requirements.bzl", "requirement")

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
        "@glog//:glog",
        "@gflags//:gflags",
        "@rules_python//python/cc:current_py_cc_libs",
    ],
    visibility = ["//visibility:public"],
)