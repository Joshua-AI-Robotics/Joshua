load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "so100_with_xbox_controller",
    srcs = ["so100_with_xbox_controller.cc"],
    data = ["//robot/config:robot_config_pbtxt"],
    deps = [
        "//robot/onboard/factory:motor_factory",
        "//utils/xbox_controller:xbox_controller",
    ],
    visibility = ["//visibility:public"],
)