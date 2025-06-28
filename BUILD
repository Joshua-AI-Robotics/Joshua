load("@rules_cc//cc:defs.bzl", "cc_binary")
load("@ubirobotics_pip_deps//:requirements.bzl", "requirement")

cc_binary(
    name = "main_program",
    srcs = ["main.cc"],
    data = ["//robot/config:robot_config_pbtxt"],
    deps = [
        "//robot/actuation/factory:motor_factory",
        "//robot/config:config_utils",
        "//utils:so100_xbox_controller_handler",
        "//robot/perception/factory:camera_factory",
        "//robot/perception/interfaces:camera_interface",
        "@com_github_google_glog//:glog",
        "@com_github_gflags_gflags//:gflags",
        "@local_deps//:highgui",
    ],
    visibility = ["//visibility:public"],
)

cc_binary(
    name = "nexus_main",
    srcs = ["nexus_main.cc"],
    data = [
        "//robot/config:robot_config_pbtxt",
        "//ai:model",
        requirement("protobuf"),
    ],
    deps = [
        "//robot/actuation/factory:motor_factory",
        "//robot/config:config_utils",
        "//robot/perception/factory:camera_factory",
        "//robot/perception/interfaces:camera_interface",
        "//robot/nexus:nexus",
        "@com_github_google_glog//:glog",
        "@com_github_gflags_gflags//:gflags",
        "@rules_python//python/cc:current_py_cc_libs",
    ],
    visibility = ["//visibility:public"],
)