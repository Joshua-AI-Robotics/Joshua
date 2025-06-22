load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "so100_with_xbox_controller",
    srcs = ["so100_with_xbox_controller.cc"],
    data = ["//robot/config:robot_config_pbtxt"],
    deps = [
        "//robot/actuation/factory:motor_factory",
        "//utils/xbox_controller:xbox_controller",
    ],
    visibility = ["//visibility:public"],
)


cc_binary(
    name = "main_camera",
    srcs = ["main_camera.cc"],
    deps = [
        "//robot/perception/factory:camera_factory",
        "//robot/perception/interfaces:camera_interface",
        "@com_github_google_glog//:glog",
        "@opencv//:highgui",
    ],
) 