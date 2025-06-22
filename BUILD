load("@rules_cc//cc:defs.bzl", "cc_binary")

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
        "@opencv//:highgui",
    ],
    visibility = ["//visibility:public"],
)