load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

cc_library(
    name = "node_manager",
    srcs = ["node_manager.cc"],
    hdrs = ["node_manager.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":runtime_observation",
        "//config:config_utils",
        "//config:validation",
        "//config/proto:config_cc_proto",
        "//ros2/proto:node_cc_proto",
        "//utils:joshua_runtime",
        "//utils:scope_exit",
        "//utils:status_macros",
        "@abseil-cpp//absl/status:status",
        "@abseil-cpp//absl/status:statusor",
        "@glog",
    ],
)

cc_library(
    name = "runtime_observation",
    srcs = ["runtime_observation.cc"],
    hdrs = ["runtime_observation.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//config/proto:config_cc_proto",
        "//launcher:version",
        "@abseil-cpp//absl/status:statusor",
        "@protobuf",
        "@ros2_deps//:cpp_rcl_interfaces",
        "@ros2_deps//:cpp_sensor_msgs",
        "@ros2_deps//:rclcpp",
    ],
)

cc_binary(
    name = "observation_fixture",
    testonly = True,
    srcs = ["observation_fixture.cc"],
    visibility = ["//tools/mcp:__pkg__"],
    deps = [
        ":runtime_observation",
        "@ros2_deps//:cpp_rcl_interfaces",
        "@ros2_deps//:cpp_sensor_msgs",
        "@ros2_deps//:rclcpp",
    ],
)

cc_test(
    name = "runtime_observation_test",
    srcs = ["runtime_observation_test.cc"],
    deps = [
        ":runtime_observation",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "node_manager_test_child",
    testonly = True,
    srcs = ["node_manager_test_child.cc"],
    deps = ["//utils:joshua_runtime"],
)

cc_test(
    name = "node_manager_test",
    srcs = ["node_manager_test.cc"],
    data = [":node_manager_test_child"],
    deps = [
        ":node_manager",
        "//utils:joshua_runtime",
        "//utils:scope_exit",
        "@googletest//:gtest_main",
        "@protobuf",
    ],
)
