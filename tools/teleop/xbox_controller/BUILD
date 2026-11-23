load("@rules_cc//cc:defs.bzl", "cc_library")
load("//:system_ros2_rules.bzl", "ros2_cpp_binary")

cc_library(
    name = "xbox_controller",
    srcs = ["xbox_controller.cc"],
    hdrs = ["xbox_controller.h"],
    deps = [
        "@glog",
        "@local_deps//:libevdev",
    ],
)

ros2_cpp_binary(
    name = "xbox_controller_publisher",
    srcs = ["xbox_controller_publisher.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":xbox_controller",
        "//config:config_utils",
        "//config:validation",
        "//ros2:logging",
        "//ros2/utils:robot_timer",
        "//utils:joshua_runtime",
        "//utils:scope_exit",
        "@ros2_deps//:cpp_std_msgs",
        "@ros2_deps//:rclcpp",
    ],
)
